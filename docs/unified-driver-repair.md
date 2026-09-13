# Repairing missing owned driver resources

Implemented; destructive VM integration coverage remains a release gate. Do not
damage the development host's input driver to exercise these cases.

Same-version repair requires the exact deterministic unified MSI identity.
Unknown/newer INF or SYS bytes, ambiguous multiple packages/nodes, unexpected
service configuration, and pending service deletion still stop before mutation.
Healthy driver repair only repairs missing filters and never reinstalls it.

When the control interface is unavailable, the initiating user's CLI first rules
out any pending profile recovery and any uncooperative configuration owner. It
then reads the actual service Parameters with strict types and bounded sizes:
Active, BlacklistedDeviceInstancePaths, WhitelistedFullImageNames and
WhitelistedInverse. The pinned driver's absent inverse value means false; missing
required values or malformed lists are rejected. Setup READY version 2 carries
control availability separately from baseline availability. The elevated worker
independently reads these values and compares the ordinary-user snapshot before
creating the protected journal. No old installation baseline is substituted.

For recognized incomplete driver resources, repair stages the pinned package
only when missing and creates a matching root node only when missing. It then
uses the supported force-install flag with the unchanged verified signed INF.
No existing device or driver package is deleted. The baseline is compared again
immediately before binding. Intent and completion are durable for every step.

Rebinding always requires a verified new boot. Resume checks exact retained or
newly created node/package identities, unchanged class filter state, and the
expected settings after the INF resets Active to false while preserving lists
and inverse mode. Missing filters are attached and can require a second restart.
Only then does the controller restore and read back the confirmed baseline.

Rollback never deletes a repaired live driver to recreate a damaged starting
state. Once binding completed, failure retains the transaction, source baseline
and maintenance exclusion for recovery. Incomplete native intents are not
blindly replayed. Existing pre-binding rollback checks still apply to completed
new resource creation.

Automated coverage includes missing node/package/service/SYS/control states,
preserved settings and unrelated filters, restart identity validation, retained
rollback boundaries, malformed stored settings, READY provenance flags and
ordinary-user maintenance recovery guards. Actual SetupAPI failure/restart and
Windows Installer rollback behavior must also be verified in the disposable
Windows environment before release.

The [Microsoft API contract](https://learn.microsoft.com/en-us/windows/win32/api/newdev/nf-newdev-updatedriverforplugandplaydevicesw)
documents forced same-package installation, source-file copying and restart
reporting. Force is used only after pinned-payload and ownership checks.
