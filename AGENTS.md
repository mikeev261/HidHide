# Agent Guidelines for HidHide

Welcome! This repository contains **HidHide**, a Gaming Input Peripherals Device Firewall for Windows. This document is designed to maximize agential performance by providing context, architectural overview, and development guidelines for this repository.

## Repository Overview

HidHide is a Windows kernel-mode filter driver (KMDF) that hides physical gamepads and joysticks from ordinary applications (like games) while allowing specific whitelisted applications (like feeder utilities such as vJoy or Joystick Gremlin) to see them. This solves the "double input" problem when using virtual controllers.

### Architecture & Components

The solution (`HidHide.sln`) is primarily written in C++ and consists of several projects:

1. **`HidHide/` (Kernel-Mode Filter Driver)**
   - The core KMDF driver.
   - Built using the Windows Driver Kit (WDK).
   - Interacts with HID class devices to selectively block read/write operations based on the application attempting access.
2. **`HidHideClient/` (GUI Configuration Utility)**
   - The user-facing application for configuring the driver.
   - Built using **C++ MFC** (Microsoft Foundation Classes).
   - Uses SetupAPI and Cfgmgr32 to query device properties.
3. **`HidHideCLI/` (Command Line Interface)**
   - A command-line tool for third-party applications or installers to interact with the HidHide configuration.
4. **`Watchdog/`**
   - Auxiliary component.
5. **`Shared/`**
   - Shared C++ headers and definitions used across both the kernel driver and user-mode applications (Client, CLI).
6. **`HidHide.Tests/`**
   - Testing project for validating logic.
7. **`build/` & `.nuke/`**
   - The build orchestration is handled by **Nuke** (a C#-based cross-platform build system).

## Key Concepts to Understand

- **Feeder Utilities**: Applications like vJoy that read from a physical device and output to a virtual device. These *must* be whitelisted in HidHide so they can see the physical devices.
- **Whitelisting**: HidHide blocks blacklisted devices from all applications *unless* the application is explicitly whitelisted.
- **Composite Devices**: USB devices often expose multiple interfaces (e.g., a keyboard that is also a mouse). HidHide can block access at the composite level. Legacy applications that try to bypass the HID layer and talk directly to the underlying driver will be blocked if all composite HID devices of that device are selected.

## Development & Modification Guidelines

- **Build Process**: The build process is orchestrated using `build.ps1` at the root. To compile the project, run `.\build.ps1`. Under the hood, this downloads the required .NET SDK, builds the Nuke orchestrator in `build\_build.csproj`, and runs MSBuild for the C++ projects.
- **Driver Development Constraints**: 
  - When making changes to the `HidHide` driver project, ensure you adhere to strict Windows kernel-mode programming constraints (e.g., proper IRQL levels, non-paged pool memory allocations when necessary, avoiding exceptions).
- **MFC Client Constraints**:
  - The UI (`HidHideClient`) relies heavily on MFC macros (`BEGIN_MESSAGE_MAP`, etc.) and resource files (`.rc`). Ensure modifications to the UI are reflected in the resource files.
- **Shared Code**: 
  - If changing the IOCTL interface or shared structs between the driver and user-mode apps, do so in the `Shared/` directory and ensure all projects recompile successfully.
- **Testing**: 
  - Ensure any new logic is covered in the `HidHide.Tests` project when applicable.

## Advanced Features Augmentation

If you are tasked with adding advanced features to this repository:
1. Identify if the feature requires driver-level enforcement (modify `HidHide` driver and `Shared` headers).
2. If it requires user configuration, expose the new settings via IOCTLs or Registry keys.
3. Update the `HidHideClient` (MFC) and `HidHideCLI` to parse and send these new configurations to the driver.
4. Use standard modern C++ (C++17/20) practices in user-mode where applicable, while adhering to KMDF C++ constraints in the driver.
