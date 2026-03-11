# Changelog

## 1.0.0 - March 11, 2026

Apollo `1.0.0` is the first cleaned-up release of this fork and matches the published GitHub release assets.

### Added

- runtime performance endpoints at `GET /api/performance` and `GET /api/performance/prometheus`
- PowerShell workload profiler at [`scripts/profile_streaming.ps1`](../scripts/profile_streaming.ps1)
- optional Tracy and OpenTelemetry integration hooks
- experimental capability surfaces for MsQuic, WebTransport, and WebCodecs

### Changed

- hardened Windows build and packaging flow for the published `v1.0.0` release
- updated version metadata across CMake and Web UI packages to `1.0.0`
- improved frame pacing and recovery behavior for common stutter cases on Windows
- added adaptive FEC, adaptive pacing, and live bitrate reconfiguration support
- exposed richer CPU, memory, packet, and GPU metrics for live profiling

### Fixed

- corrected Windows resource path handling during builds
- removed several stale or non-portable build assumptions from the optimization layer
- aligned release-facing docs with the current Apollo fork and published assets

### Release Assets

- [`Apollo-Windows-AMD64-installer.exe`](https://github.com/marcusbooker77/Apollo/releases/download/v1.0.0/Apollo-Windows-AMD64-installer.exe)
- [`Apollo-Windows-AMD64-portable.zip`](https://github.com/marcusbooker77/Apollo/releases/download/v1.0.0/Apollo-Windows-AMD64-portable.zip)

### Notes

- the primary Windows executable is still `sunshine.exe`
- package-manager entries may lag behind this fork; GitHub Releases is the authoritative distribution channel for `1.0.0`

<div class="section_buttons">

| Previous                              |                          Next |
|:--------------------------------------|------------------------------:|
| [Getting Started](getting_started.md) | [Docker](../DOCKER_README.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
