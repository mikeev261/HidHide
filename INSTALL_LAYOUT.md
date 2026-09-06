# Install layout (canonical)

The App Profiles installer is a user-mode companion for an existing, Microsoft-signed HidHide installation. It must never deploy, replace, or remove the kernel driver.

## Rules

- **Root directory:** `%ProgramFiles%\HidHide App Profiles\`
- **Do not** add `x64`, `ARM64`, or similar architecture segments under that root.
- **Do not** use the legal suffix `e.U.` in **any filesystem path** (trailing-dot folder names break Windows and MSI tooling). Use **`Nefarius Software Solutions`** for on-disk folders; keep full legal names only in MSI metadata (Manufacturer, ARP), not directory names.

## Typical payload (same relative paths per platform)

| Relative path | Purpose |
|---------------|---------|
| `HidHideClient.exe` | Configuration UI and resident profile manager |
| `HidHideCLI.exe` | CLI, including profile configuration commands |

The existing HidHide driver continues to load from `%WinDir%\System32\drivers\`. The companion MSI contains no INF, CAT, SYS, class-filter tool, or driver custom action.

## Building the MSI (WixSharp, OSS)

This repo uses the same stack as [WinDbgSymbolsCachingProxy](https://github.com/nefarius/WinDbgSymbolsCachingProxy): **`WixSharp_wix4`** (NuGet) driving the **WiX 5** toolchain.

### Prerequisites (Windows)

Install the **WiX 5.0.2** .NET global tool and the matching UI extension. **Do not** default to WiX 6 without validating WixSharp + `WixUI_*`: newer `wix` defaults are not compatible with the WixSharp + `WixUI_FeatureTree` combination without version tweaks (same pitfall as documented in WinDbgSymbolsCachingProxy).

```powershell
dotnet tool install --global wix --version 5.0.2
wix extension add -g WixToolset.UI.wixext/5.0.2
```

The default companion build is `.\build.ps1 Ci --configuration Release --platform x64`. See [MAINTENANCE.md](MAINTENANCE.md) for build scope and the executable-before-MSI release signing workflow.

### Staging directory

Collect `HidHideClient.exe` and `HidHideCLI.exe` into one flat staging folder.

### Build

```powershell
cd Installer
dotnet run -c Release -- --staging "C:\path\to\staging" --out "C:\path\to\msi-out" --platform x64
```

Optional environment variables: `HIDHIDE_INSTALLER_STAGING`, `HIDHIDE_INSTALLER_OUT`.

The generated `HidHideAppProfiles.msi` installs alongside the official HidHide package and does not change the driver installation.
