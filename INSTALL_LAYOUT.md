# Installation layout

Current target: HidHide (mikeev261 fork), Windows 11 x64. One Burn setup EXE owns
one private per-machine MSI; only the bundle is visible in Installed Apps.
Release readiness and supported paths are tracked in docs/release-readiness.md.

| Location | Owned content |
|---|---|
| %ProgramFiles%\HidHide | HidHideClient.exe, HidHideCLI.exe, app-local Microsoft VC/MFC runtime DLLs |
| %ProgramFiles%\HidHide\Driver | Verified unchanged INF, SYS, CAT and original LICENSE.rtf |
| Windows driver store / System32\drivers | Native installation of the same verified upstream driver |
| Start menu\HidHide | One shortcut to the enhanced configuration UI |
| %ProgramData%\mikeev261\HidHide\Maintenance | Protected setup cache, journals and recovery evidence |

Profiles retain their existing per-user registry locations. The initiating user's
owned startup entry is updated only after verification; startup preference is
preserved. Never run the resident configuration manager as SYSTEM or elevated.

Run the setup normally, then approve elevation. Do not install the private MSI
independently. After a restart-required result, restart when convenient and rerun
that exact setup to finish verification. Keep the setup and recovery material.
Do not clear maintenance markers or delete registry entries to resolve failures.

The legacy companion lived in HidHide App Profiles and required a separate driver;
that layout and its packaging restrictions are superseded by the unified design.
