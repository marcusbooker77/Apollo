# Apollo

Apollo is a desktop streaming host fork focused on low-latency Windows streaming with built-in virtual display workflows, per-client permissions, and a local web UI. It supports AMD, Intel, and NVIDIA hardware encoding, plus software encoding when needed.

## Current Status

- Stable release: [`v1.0.0`](https://github.com/marcusbooker77/Apollo/releases/tag/v1.0.0)
- Official release channel: [GitHub Releases](https://github.com/marcusbooker77/Apollo/releases)
- Official binaries currently published by this fork: Windows installer and portable zip
- Primary Windows executable name: `sunshine.exe`
- Virtual display support is Windows-first

## Key Features

- Built-in virtual display with per-client resolution and refresh matching
- HDR-capable streaming workflow on supported Windows systems
- Client permission management
- Clipboard sync
- Commands on client connect and disconnect
- Input-only mode
- Runtime performance metrics and profiling endpoints
- Optional tracing and experimental browser transport lab surfaces

## Download

Use the official GitHub release assets for Apollo `1.0.0`:

- [`Apollo-Windows-AMD64-installer.exe`](https://github.com/marcusbooker77/Apollo/releases/download/v1.0.0/Apollo-Windows-AMD64-installer.exe)
- [`Apollo-Windows-AMD64-portable.zip`](https://github.com/marcusbooker77/Apollo/releases/download/v1.0.0/Apollo-Windows-AMD64-portable.zip)

Community package-manager entries may lag behind this fork or track a different Apollo/Sunshine lineage. GitHub Releases is the authoritative distribution channel for this repo.

## Quick Start

1. Download the installer or portable zip from [Releases](https://github.com/marcusbooker77/Apollo/releases).
2. Start Apollo on the host machine.
3. Open `https://localhost:47990` and complete first-run setup.
4. Pair your Artemis or Moonlight-compatible client.
5. Configure applications, audio/video options, and permissions in the web UI.

## Documentation

Start with the docs in this repository:

- [Getting Started](docs/getting_started.md)
- [Configuration](docs/configuration.md)
- [Performance Tuning](docs/performance_tuning.md)
- [Building](docs/building.md)
- [Changelog](docs/changelog.md)

## Windows Notes

### Virtual Display

Apollo uses SudoVDA for its Windows virtual display workflow. For the cleanest behavior, remove other virtual display tools and old Sunshine/Apollo virtual-display configurations before switching over.

The virtual display is created when a stream starts and removed when it ends. Apollo keeps a stable display identity per client so Windows can remember display-specific resolution and layout settings instead of treating every session as a new monitor.

### Dual GPU Laptops

If you want to render and encode on the dGPU, set `Adapter Name` to the discrete GPU and enable `Headless mode` in the `Audio/Video` settings.

### HDR

HDR support starts on Windows 11 23H2 and is generally more reliable on 24H2. Good HDR results still depend heavily on the client device, calibration, and game support. SDR remains the safer default if you want predictable results.

## Legacy Wiki Pages

Some topic-specific material still lives in the original Apollo wiki:

- [Permission System](https://github.com/ClassicOldSong/Apollo/wiki/Permission-System)
- [Auto Pause / Resume Games](https://github.com/ClassicOldSong/Apollo/wiki/Auto-pause-resume-games)
- [Multiple Instances](https://github.com/ClassicOldSong/Apollo/wiki/How-to-start-multiple-instances-of-Apollo)
- [FAQ](https://github.com/ClassicOldSong/Apollo/wiki/FAQ)
- [Stuttering Clinic](https://github.com/ClassicOldSong/Apollo/wiki/Stuttering-Clinic)

## Requirements

High-level host requirements:

- Windows 10 or newer for the primary Apollo workflow
- AMD, Intel, or NVIDIA GPU with supported hardware encoding, or enough CPU for software encode
- Reliable local network, preferably wired or strong 5 GHz Wi-Fi

See [Getting Started](docs/getting_started.md) for broader platform notes inherited from the upstream codebase.

## Integrations

- [Artemis (Moonlight Noir)](https://github.com/ClassicOldSong/moonlight-android) for client-side virtual display control
- SudoVDA for Windows virtual display management

Artemis currently targets Android first. Other client/platform support depends on the client project rather than this host repo.

## Support

Project support is handled through GitHub:

- [Issues](https://github.com/marcusbooker77/Apollo/issues)

No real-time chat support is provided for this fork.

## License

GPLv3
