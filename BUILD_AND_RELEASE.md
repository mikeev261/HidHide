# Build and release (maintainers)

The root `README.md` is reserved for end-user documentation. Maintainer notes for the BthPS3-style pipeline live here.

## Flow

- **CI (AppVeyor)** produces unsigned user-mode companion installers: `HidHideAppProfiles_x64.msi` and `HidHideAppProfiles_ARM64.msi`.
- The companion installer never packages or replaces the HidHide kernel driver. Install an official Microsoft-signed HidHide release first.
- **Local** maintainer machines may Authenticode-sign the MSI and user-mode executables before publishing; this is independent of Secure Boot kernel signing.

## CI build entrypoint

```powershell
.\build.ps1 Ci --configuration Release --platform x64
.\build.ps1 Ci --configuration Release --platform ARM64
```

## Installer payload

The MSI builder consumes a flat per-arch staging directory; see [INSTALL_LAYOUT.md](INSTALL_LAYOUT.md).

## Local signing

Download CI artifacts for a tag/version and sign the MSI files (AppVeyor API token required):

```powershell
.\release.ps1 -BuildVersion v1.2.3 -Token <appveyor_token>
```

Default certificate subject is `Nefarius Software Solutions` (override with `-CertName` if needed).

## Companion build and test pipeline (package 01)

Run from the repository root in a Visual Studio developer shell:

```powershell
./build.ps1 UnitTest --configuration Release --platform x64
./build.ps1 Compile --configuration Release --platform ARM64
```

The default target is `UnitTest`: it restores Google Test, explicitly rebuilds
GUI, CLI, and tests, and executes tests on a compatible Windows host. `Ci` adds
companion MSI packaging. Neither path builds the driver or attestation CAB.
`CompileDriver` and `BuildCab` are opt-in driver-development targets requiring
WDK and its corresponding toolset. The orphaned Watchdog solution mappings
were removed; Watchdog is not part of the companion payload.

MSBuild must be on PATH, or pass `--compiler-path <absolute MSBuild.exe path>`.
This also avoids NUKE 9's discovery limitation with Visual Studio 18. Build logs
are in `artifacts/logs/<architecture>/*.binlog`; Google Test XML is in
`artifacts/tests/<architecture>/results.xml`. A nonzero test exit fails NUKE,
the PowerShell entrypoint, and the AppVeyor build-script step. CI validates all
branches and PRs, uploads XML results, and retains logs. Publication is separate.

Required tools: Visual Studio with v145 C++ x64 and ARM64 build tools, matching
MFC/ATL (including Spectre libraries used by these projects), Windows SDK,
and .NET SDK compatible with `global.json`. Installer builds additionally use
.NET Framework 4.8 targeting tools, WixSharp 2.13.0, WiX CLI 5.0.2 and its UI
extension 5.0.2. The verified local x64 configuration on 2026-09-07 used MSBuild
18.6.3, MSVC/MFC 14.51.36231, Windows SDK 10.0.28000.0, and .NET SDK 10.0.303.
The current personal-use target is Windows 11 x64. ARM64 tools and validation are
out of scope; the ARM64 commands above are optional future release work.

Clean verification uses a fresh source checkout (or `git archive` expanded into
an empty directory with the reviewed changes applied). Change directory into
that checkout before invoking `build.ps1`; NUKE locates its root from the current
directory. Do not copy `bin`, `obj`, `packages`, staging, or installer outputs.
Run the two commands above. To verify failure propagation, add an intentionally
failing Google Test only in the disposable checkout, rerun `UnitTest`, and
confirm a nonzero exit. Never commit that injected failure.
