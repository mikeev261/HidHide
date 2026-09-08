# Maintenance scope

This repository builds a unified HidHide fork for Windows 11 x64: enhanced MFC
configuration UI/App Profiles, CLI, and an unchanged verified Microsoft-signed
upstream driver. The installer manages the signed driver's lifecycle; archival
kernel source remains excluded from the solution/build and is not maintained here.
Exported IOCTL contracts, original signed files, Secure Boot and signature
validation remain unchanged. No test certificates or trust-store changes.

See BUILD_AND_RELEASE.md for unified CI, explicit compiler-path support, offline
payload inputs and optional signing. See INSTALL_LAYOUT.md for ownership and
user startup behavior. Existing companion-only instructions are historical.

The driver uses a global hidden-device list. Preserve each user's source profile
configuration and confirmed baseline; active-profile effective settings are not
a baseline backup. Maintenance quiesces the ordinary-user coordinator before
requesting elevation. Other-user ownership blocks changes. All machine mutations
require protected transaction evidence and exclusion, never user-controlled paths.

Keep the exact setup used for install and all protected recovery journals. After
3010, reboot and rerun that setup. Unknown or incomplete native operations must
be inspected and reconciled explicitly; never edit a journal to invent completion,
remove uninstall registry entries, or run arbitrary driver removal commands.

Before distributing, complete docs/release-readiness.md against the exact final
source and artifact. Current status: docs/unified-package-progress.md. No automatic
release publication or merge is part of the build workflow.
