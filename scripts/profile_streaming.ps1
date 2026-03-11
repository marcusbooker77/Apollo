param(
    [string]$BaseUrl = "https://localhost:47990",
    [Parameter(Mandatory = $true)]
    [string]$Username,
    [Parameter(Mandatory = $true)]
    [string]$Password,
    [int]$DurationSeconds = 60,
    [int]$SampleIntervalMs = 1000,
    [string]$ProcessName = "sunshine",
    [string]$OutputDirectory = (Join-Path (Get-Location) ("profiling-" + (Get-Date -Format "yyyyMMdd-HHmmss")))
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function New-HttpClient {
    $handler = [System.Net.Http.HttpClientHandler]::new()
    $handler.CookieContainer = [System.Net.CookieContainer]::new()
    $handler.ServerCertificateCustomValidationCallback = { $true }

    $client = [System.Net.Http.HttpClient]::new($handler)
    $client.Timeout = [TimeSpan]::FromSeconds(10)
    return @{
        Client = $client
        Handler = $handler
    }
}

function Invoke-JsonRequest {
    param(
        [Parameter(Mandatory = $true)]
        [System.Net.Http.HttpClient]$Client,
        [Parameter(Mandatory = $true)]
        [string]$Method,
        [Parameter(Mandatory = $true)]
        [string]$Url,
        [object]$Body
    )

    $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::$Method, $Url)
    if ($null -ne $Body) {
        $json = $Body | ConvertTo-Json -Depth 8 -Compress
        $request.Content = [System.Net.Http.StringContent]::new($json, [System.Text.Encoding]::UTF8, "application/json")
    }

    $response = $Client.Send($request)
    $content = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    if (-not $response.IsSuccessStatusCode) {
        throw "Request to $Url failed with $([int]$response.StatusCode): $content"
    }

    if ([string]::IsNullOrWhiteSpace($content)) {
        return $null
    }

    return $content | ConvertFrom-Json -Depth 8
}

function Get-ProcessSample {
    param(
        [string]$Name,
        [Nullable[double]]$PreviousCpuSeconds,
        [datetime]$PreviousTimestamp
    )

    $proc = Get-Process -Name $Name -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -First 1
    if ($null -eq $proc) {
        return $null
    }

    $now = Get-Date
    $cpuPercent = $null
    if ($null -ne $PreviousCpuSeconds -and $null -ne $PreviousTimestamp) {
        $wallSeconds = ($now - $PreviousTimestamp).TotalSeconds
        if ($wallSeconds -gt 0) {
            $cpuPercent = (($proc.CPU - $PreviousCpuSeconds) / $wallSeconds) * 100.0 / [Environment]::ProcessorCount
        }
    }

    return @{
        Timestamp = $now
        ProcessId = $proc.Id
        CpuSeconds = [double]$proc.CPU
        CpuPercent = $cpuPercent
        WorkingSetBytes = [int64]$proc.WorkingSet64
        PrivateMemoryBytes = [int64]$proc.PrivateMemorySize64
        Handles = [int]$proc.Handles
        Threads = [int]$proc.Threads.Count
    }
}

function Write-Json {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [object]$Value
    )

    $Value | ConvertTo-Json -Depth 10 | Set-Content -Path $Path -Encoding utf8
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$http = New-HttpClient
$client = $http.Client

$loginBody = @{
    username = $Username
    password = $Password
}

[void](Invoke-JsonRequest -Client $client -Method "Post" -Url ($BaseUrl.TrimEnd("/") + "/api/login") -Body $loginBody)

$samples = New-Object System.Collections.Generic.List[object]
$startedAt = Get-Date
$deadline = $startedAt.AddSeconds($DurationSeconds)
$previousCpuSeconds = $null
$previousTimestamp = $null

while ((Get-Date) -lt $deadline) {
    $timestamp = Get-Date
    $metrics = Invoke-JsonRequest -Client $client -Method "Get" -Url ($BaseUrl.TrimEnd("/") + "/api/performance") -Body $null
    $processSample = Get-ProcessSample -Name $ProcessName -PreviousCpuSeconds $previousCpuSeconds -PreviousTimestamp $previousTimestamp

    if ($null -ne $processSample) {
        $previousCpuSeconds = $processSample.CpuSeconds
        $previousTimestamp = $processSample.Timestamp
    }

    $samples.Add([pscustomobject]@{
        timestamp = $timestamp.ToString("o")
        metrics = $metrics
        process = if ($null -ne $processSample) {
            [pscustomobject]@{
                pid = $processSample.ProcessId
                cpu_seconds = $processSample.CpuSeconds
                cpu_percent = $processSample.CpuPercent
                working_set_bytes = $processSample.WorkingSetBytes
                private_memory_bytes = $processSample.PrivateMemoryBytes
                handles = $processSample.Handles
                threads = $processSample.Threads
            }
        } else {
            $null
        }
    })

    Start-Sleep -Milliseconds $SampleIntervalMs
}

$summary = [pscustomobject]@{
    base_url = $BaseUrl
    process_name = $ProcessName
    started_at = $startedAt.ToString("o")
    finished_at = (Get-Date).ToString("o")
    duration_seconds = $DurationSeconds
    sample_interval_ms = $SampleIntervalMs
    sample_count = $samples.Count
    latest_metrics = if ($samples.Count -gt 0) { $samples[$samples.Count - 1].metrics } else { $null }
    peak_process_cpu_percent = ($samples | ForEach-Object { $_.process.cpu_percent } | Where-Object { $null -ne $_ } | Measure-Object -Maximum).Maximum
    peak_working_set_bytes = ($samples | ForEach-Object { $_.process.working_set_bytes } | Where-Object { $null -ne $_ } | Measure-Object -Maximum).Maximum
    peak_private_memory_bytes = ($samples | ForEach-Object { $_.process.private_memory_bytes } | Where-Object { $null -ne $_ } | Measure-Object -Maximum).Maximum
}

Write-Json -Path (Join-Path $OutputDirectory "samples.json") -Value $samples
Write-Json -Path (Join-Path $OutputDirectory "summary.json") -Value $summary

$prometheus = $client.GetStringAsync($BaseUrl.TrimEnd("/") + "/api/performance/prometheus").GetAwaiter().GetResult()
Set-Content -Path (Join-Path $OutputDirectory "metrics.prom") -Value $prometheus -Encoding utf8

Write-Host "Profiling complete."
Write-Host "Output directory: $OutputDirectory"
Write-Host "Samples: $($samples.Count)"
