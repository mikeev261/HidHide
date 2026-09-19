# Agent guidelines for unified HidHide

The unified package design supersedes all historical companion-only restrictions.
Read INSTALL_LAYOUT.md, MAINTENANCE.md, docs/unified-package-design.md and
current docs/unified-package-progress.md before changing installation or scope.

- One public Windows 11 x64 MSI owns one visible HidHide Profiles entry and uses the standard WiX MSI dialogs.
- Package/manage only the exact verified Microsoft-signed upstream driver.
- Never modify kernel code, exported IOCTL contracts, signed payload, Secure Boot,
  trust stores, or test signing. HidHide/ is archival and excluded from builds.
- Preserve profile definitions, baseline, unrelated filters and initiating-user
  ownership. Feeders must remain whitelisted to read hidden physical devices.
- MFC resources and message maps must remain consistent with UI changes.
- Run meaningful native and managed checks, and the unified Ci target described
  in BUILD_AND_RELEASE.md. No WDK or ARM64 tools are needed for supported x64.
- Installer owns the public MSI and its user/elevated action boundary;
  Installer.Driver owns native lifecycle. Installer.Bootstrapper and
  Installer.Controller are historical recovery references and are not shipped.
  Keep authorization boundaries intact.
- Native integration tests change machine state: preserve recovery evidence and
  use authorized environments. Never delete uninstall entries to mimic migration.
- Keep progress concise and distinguish build success, host/VM validation, and
  public release readiness. Do not publish or merge automatically.

Historical companion implementation remains reference material, not the current
product scope. See docs/unified-package-history.md for implementation history.
