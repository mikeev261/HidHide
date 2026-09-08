# Build and release

The supported package is one unified Windows 11 x64 Burn EXE. Public release is
not yet validated; see docs/release-readiness.md. Kernel code is never built.

Prerequisites: Visual Studio C++/MFC x64 tools, Windows SDK (including signtool),
.NET SDK, PowerShell 7 (pwsh), WiX 5.0.2 CLI and matching UI extension. No WDK or ARM64 tools required.

```powershell
.\build.ps1 Ci --configuration Release --platform x64 --compiler-path "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
```

Ci builds applications and tests, executes native/managed tests, acquires/verifies
the pinned signed driver and produces the unified bundle. Use --driver-payload,
--upstream-recovery and --sign-tool for explicit local verified inputs/tool paths.
HIDHIDE_DRIVER_PAYLOAD and HIDHIDE_UPSTREAM_RECOVERY are supported equivalents.
Keep all recovery evidence in artifacts; never clear the entire directory.

Release uses a new output directory and already-built version-consistent apps:

```powershell
.\release.ps1 -Staging .\bin\Release\x64 -DriverPayload .\artifacts\unified-driver-verified -UpstreamRecovery .\artifacts\unified-empty-cache\HidHide_1.5.230_x64.exe -Companion1Recovery <full-companion-1-msi> -Companion99Recovery <full-companion-99-msi> -Out .\artifacts\release-candidate -NoSigning -SignTool <signtool.exe>
```

The current release decision is explicitly unsigned owned setup/application code.
This never means unsigned kernel code: catalog trust/membership and exact driver
hashes are mandatory. Optional signing omits -NoSigning and supplies -CertName.
Only owned code is signed; the upstream driver is never re-signed or regenerated.
The pipeline signs owned code before packaging, then MSI and the detached Burn
engine/final bundle using WiX's signing sequence. No upload happens automatically.

Inspect release-manifest.json, logs and test XML, then validate the exact final
artifact. A successful build does not close native upgrade, recovery, offline,
physical-device or alternate-user acceptance gates. Dirty source evidence must
not be represented as an exact clean source commit.

Historical companion-only MSI build commands are superseded.
