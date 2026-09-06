# Agent Guidelines for HidHide App Profiles

This repository maintains a user-mode companion to an independently installed Microsoft-signed HidHide driver. Read [INSTALL_LAYOUT.md](INSTALL_LAYOUT.md) and [MAINTENANCE.md](MAINTENANCE.md) before changing build, release, installation, or product scope.

## Components

- `HidHideClient/`: C++ MFC configuration UI and resident app profile manager. Keep resource files and MFC message maps consistent with UI changes.
- `HidHideCLI/`: user-mode configuration CLI.
- `HidHide.Tests/`: tests for companion logic. Cover new logic here when applicable.
- `Shared/`: shared headers and exported driver contracts; preserve compatibility with the separately installed driver and external consumers.
- `Installer/`: WixSharp companion MSI containing only Client and CLI.
- `build/` and `.nuke/`: NUKE build orchestration.
- `HidHide/`: archival kernel source, intentionally excluded from the default solution/build. Do not add new kernel enforcement or change exported IOCTL behavior as part of companion work.

## Development

Run `.\build.ps1 Ci --configuration Release --platform x64` to build, test, and package the companion. ARM64 requires the corresponding C++/MFC tools; ARM64 test executables cannot run on an x64 CI host. See MAINTENANCE.md for prerequisites and release signing.

The signed driver exposes a global hidden-device list. The companion manages profiles in user mode; feeder utilities must remain whitelisted to read hidden physical devices. Preserve the user's baseline configuration and compatibility with existing driver interfaces. Installer changes must never deploy, replace, or remove the kernel driver.

Use modern C++ in user-mode code. A future driver maintenance effort requires an explicit scope decision and a dedicated kernel verification plan; retained source is reference material, not an invitation to expand this product's scope.
