# Maintenance scope

This repository maintains **HidHide App Profiles**, a user-mode companion to the independently installed Microsoft-signed HidHide driver. The default `HidHide.sln` contains only Client, CLI, and their tests; NUKE `Ci` builds/tests those projects and packages the two companion executables. No WDK, driver signing, attestation CAB, or driver installation is part of that path. See [INSTALL_LAYOUT.md](INSTALL_LAYOUT.md).

## Retained driver reference

`HidHide/` and the exported contracts in `Shared/` are retained unchanged as historical/reference source, outside the default solution and build. This repository is **not maintaining or distributing a custom kernel fork**. The retained project is not a supported standalone build/release path; the separately installed official driver supplies enforcement.

Consequently issue #12's conditional kernel rewrite is not undertaken: the recursive PID tree and boot self-test remain historical source, not companion deliverables. Reopening driver maintenance requires a separate explicit scope decision and kernel verification plan covering process churn, PID reuse, allocations, synchronization, cache invalidation, and IRQL constraints.

No external-consumer inventory has established that the exported session-blacklist IOCTLs are unused. Their definitions and implementation are preserved; absence of repository client calls does not justify breaking external consumers.

## Removed infrastructure

The unused Watchdog diagnostics service, HTTP/ETW server, and dedicated vcpkg submodule/dependencies were removed. They were absent from the companion solution and installer. Their history remains available in Git. Legacy staging-driver test signing, nefcon download helpers, attestation DDF/CAB generation, and `stage0.ps1` were removed as well.

The inherited Winget publisher targeted the upstream driver identity `Nefarius.HidHide`. It was removed; this companion must not publish updates under that identity. The inherited tag-triggered AppVeyor `BUILDBOT` deployment environment was also removed because it has no defined companion release contract here. No replacement package identity or publishing automation is assumed.

## Companion builds and releases

On Windows, install Visual Studio C++/MFC, the Windows SDK, .NET 10 SDK, and the WiX prerequisites in INSTALL_LAYOUT.md. Run:

```powershell
.\build.ps1 Ci --configuration Release --platform x64
# With ARM64 C++/MFC tools installed (tests cannot execute on an x64 host):
.\build.ps1 Ci --configuration Release --platform ARM64
```

CI MSIs are unsigned. For a signed release, use the corresponding CI-built Client and CLI executables in a flat local folder. `release.ps1` no longer downloads artifacts from an assumed upstream AppVeyor account; obtain the intended companion build explicitly. Run once per architecture with a new, empty output directory:

```powershell
.\release.ps1 -Staging .\bin\Release\x64 -Platform x64 -Out .\artifacts\release-x64 -CertName 'Your code-signing certificate subject' -SignTool 'C:\path\to\signtool.exe'
```

The script copies and signs/verifies both executables, rebuilds the MSI from those signed copies, then signs/verifies the MSI. It fails on missing inputs, signing/verification failures, or installer failure. Input and output directories must be disjoint. `-NoSigning` builds an explicitly unsigned package without requiring a certificate or SignTool. The MSI version comes from the staged Client executable, not a download/build label. Signing requires the release operator's certificate and Windows SDK SignTool; no WDKWhere dependency remains. Nothing is uploaded or published by the script.
