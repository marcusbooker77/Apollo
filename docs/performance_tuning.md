# Performance Tuning
In addition to the options available in the [Configuration](configuration.md) section, there are a few additional
system options that can be used to help improve the performance of Sunshine.

## Profiling A Real Stream

Apollo now exposes authenticated runtime metrics through the configuration UI and includes a sampling script for
capturing those metrics during an actual stream.

### Metrics Endpoints

After logging into the configuration UI, the following endpoints are available:

- `GET /api/performance` returns JSON metrics
- `GET /api/performance/prometheus` returns Prometheus-formatted metrics

These endpoints include frame capture time, encode time, end-to-end frame time, transmitted frame counts, packet
statistics, and basic resource usage fields.

### Windows Profiling Script

Use [`scripts/profile_streaming.ps1`](../scripts/profile_streaming.ps1) while a real client is connected:

```powershell
pwsh -File .\scripts\profile_streaming.ps1 `
  -Username "admin" `
  -Password "your-password" `
  -DurationSeconds 120 `
  -SampleIntervalMs 1000
```

By default the script connects to `https://localhost:47990`, logs into the Web UI, samples `/api/performance`,
captures `sunshine.exe` process CPU and memory usage, and writes:

- `samples.json` for full time-series data
- `summary.json` for peak and final values
- `metrics.prom` for Prometheus ingestion or diffing

Recommended workflow:

1. Start Apollo.
2. Begin a representative client session.
3. Run the profiling script for 60-300 seconds.
4. Compare `summary.json` and the last entries in `samples.json` between workloads, codecs, resolutions, and client counts.

## Optional Tracing Integrations

Apollo now also includes optional integrations for Tracy and OpenTelemetry:

- `APOLLO_ENABLE_TRACY=ON` adds targeted profiler zones to encode, FEC, and broadcast hot paths
- `APOLLO_ENABLE_OPENTELEMETRY=ON` enables trace export when an OTLP endpoint is configured with `APOLLO_OTEL_EXPORTER_OTLP_ENDPOINT` or `OTEL_EXPORTER_OTLP_ENDPOINT`

These integrations are disabled by default so the normal build remains lightweight.

## Experimental Browser Transport Lab

Apollo exposes an authenticated experimental lab at `/experimental` together with:

- `GET /api/experimental/features`

The page reports:

- server-side Tracy, OpenTelemetry, and MsQuic capability state
- browser support for WebTransport, `VideoDecoder`, and `VideoEncoder`
- `VideoDecoder.isConfigSupported(...)` results for representative H.264, HEVC, and AV1 decoder configurations

This is meant for validation and prototyping. It does not replace the primary native streaming path.

## AMD

In Windows, enabling *Enhanced Sync* in AMD's settings may help reduce the latency by an additional frame. This
applies to `amfenc` and `libx264`.

## NVIDIA

Enabling *Fast Sync* in Nvidia settings may help reduce latency.

<div class="section_buttons">

| Previous            |          Next |
|:--------------------|--------------:|
| [Guides](guides.md) | [API](api.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
