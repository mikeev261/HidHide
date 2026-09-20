# Maintenance scope

This repository builds HidHide Profiles for Windows 11 x64: Electron/React
profile editor, native ordinary-user coordinator and CLI, and an unchanged verified Microsoft-signed
upstream driver. The installer manages the signed driver's lifecycle; archival
kernel source remains excluded from the solution/build and is not maintained here.
Exported IOCTL contracts, original signed files, Secure Boot and signature
validation remain unchanged. No test certificates or trust-store changes.

The production coordinator has no MFC Profiles page. Its hidden MFC message loop
owns native enforcement and tray commands. Restart and recovery acceptance uses
the current editor service without profile windows; the device-coalescer fixture
uses only a hidden engine window. The editor uses an authenticated same-user native bridge
and cannot elevate the coordinator. The packaged renderer has no Node access or
remote content. See docs/electron-editor.md for UI validation and process lifetime.

See BUILD_AND_RELEASE.md for unified CI, explicit compiler-path support, offline
payload inputs and optional signing. See INSTALL_LAYOUT.md for ownership and
user startup behavior. Existing companion-only instructions are historical.

The driver uses a global hidden-device list. Preserve each user's source profile
configuration and confirmed baseline; active-profile effective settings are not
a baseline backup. Maintenance quiesces the coordinator through an authenticated
ordinary-user helper before protected driver mutations. Windows Installer may
already have obtained consent; its elevated token must never become the helper's
token. Other-user ownership blocks changes. All machine mutations
require protected transaction evidence and exclusion, never user-controlled paths.

Keep the exact MSI used for install and all protected recovery journals. When
Windows Installer reports a restart, reboot so its suspended transaction can
resume. Unknown or incomplete native operations must
be inspected and reconciled explicitly; never edit a journal to invent completion,
remove uninstall registry entries, or run arbitrary driver removal commands.
Uninstall retains the registered product and application files through the native
restart checkpoint. A retry before restart must preserve the earlier protected
transaction. Setup reports maintenance failures through Windows Installer's error
UI and log, with recovery evidence retained.

Before distributing, complete docs/release-readiness.md against the exact final
source and artifact. Current status: docs/unified-package-progress.md. No automatic
release publication or merge is part of the build workflow.
