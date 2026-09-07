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
