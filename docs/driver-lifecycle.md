# Driver lifecycle backend and private MSI preview

Status: implemented backend/transaction hooks and Burn development preview; **not a lifecycle-validated release**.
The build produces a reviewable private MSI preview. Do not distribute or install
it as a finished product. Burn preparation and conservative completion/recovery are implemented in [setup-controller.md](setup-controller.md); native lifecycle validation remains required.

## Implemented

`Installer.Driver` builds `HidHide.DriverSetup.exe` for x64/.NET Framework 4.8.
It uses Windows SetupAPI/NewDev directly, with no third-party helper, shell commands,
service executable or locally rebuilt kernel driver.

- Native read-only detection includes present/non-present matching root devices,
  exact published OEM INF identities, pinned INF/SYS hashes, service identity,
  all three filter lists, control availability and a raw driver-settings snapshot.
- Stage the exact package, create/register one root node, then bind the signed INF.
  Duplicate nodes, unknown matching packages and unexpected service configurations
  fail closed. No force-install or force-uninstall flags are used.
- Attach filters idempotently. Removal preserves unrelated entries and order.
  Every filter edit compares the current value with its expected value and reads
  back the result. Missing and empty values remain distinct for rollback.
- Healthy repair/upgrade retains the existing driver and settings. Missing filter
  registration can be repaired without rebinding. Missing/broken device, service,
  binary or inaccessible control requires explicit recovery rather than guessing
  a baseline or resetting the INF's Active setting.
- Uninstall detaches and verifies all three classes before node/package deletion.
  Only the recorded exact node and pinned `oemNN.inf` can be removed. Residual
  resources are an incomplete outcome, not successful uninstall.
- Native BOOL reboot results stop further operations. Filter edits also request
  a reboot because existing device stacks may retain their prior filter stack.
  No reboot is initiated.

The public worker supports `--inspect`, `--apply <transaction-guid>` and
`--rollback <transaction-guid>`. Mutation needs an elevated token, a protected
journal, pinned payload in the fixed protected cache, and the retained maintenance
barrier/coordinator lease. There is no command to manufacture an approved journal
from ordinary-user arguments. The MSI passes only a GUID, never commands or paths.

## Journal and exclusion

Journals use schema 1 and a transaction GUID under
`%ProgramData%\mikeev261\HidHide\Maintenance`. They contain operation/status,
initiating SID, pinned payload identity, the original resource/settings snapshot,
and intent/completion/undo records. They contain sensitive baseline data.

The directory and journal DACLs permit only Administrators and SYSTEM; owner and
ACL shape are checked. Reparse paths, oversized data, unknown schemas and invalid
identities are rejected. Writes flush a newly created pending file and atomically
replace the journal with write-through semantics. A leftover pending file stops
replay. Actual protected-directory writes and power-loss durability have not yet
been exercised by an elevated setup controller.

Before mutations, the worker creates a protected, ordinary-user-readable marker:
`HKLM\SOFTWARE\mikeev261\HidHide\Maintenance`. GUI/CLI admission checks its
presence even when the event no longer exists. Empty/incomplete markers and read
errors block configuration. Worker exit does not clear it. Only the verified final
verification/recovery controller may clear it after confirming the entire outcome.

## Failure and rollback boundaries

Every native call follows a flushed intent record. Completion is written after
return/read-back. A failed or timed-out call may have changed Windows state, so its
uncompleted intent is never automatically replayed or assigned ownership.

Known completed creation can roll back in reverse order, comparing filter values
before undo and detaching before removing owned nodes/packages. Unexpected external
filter changes stop rollback. A reboot-required outcome or interrupted native call
retains the journal and marker for explicit re-detection. Uninstall after node or
package removal needs cached reinstall and baseline restoration; this is deliberately
not represented as an automatic successful rollback.

MSI runs each worker in a separate hidden process with a 120-second wait and a
five-second termination wait. Timeout fails MSI, retains durable exclusion and
requires re-detection; terminating the caller does not prove the OS operation was
cancelled. Console logs are bounded and omit baseline settings.

## MSI integration and build

The `--unified-preview` builder path uses the fixed unified upgrade family,
private ARP registration, one shortcut, and `%ProgramFiles%\HidHide`. It packages
the applications and original INF/SYS/CAT/license. The worker is embedded in the
managed custom-action cabinet. The normal builder remains the historical companion
path until the complete Burn build replaces it.

Install/repair actions run after InstallFiles; uninstall runs before RemoveFiles.
Each checked, deferred, non-impersonating action has a rollback action scheduled
before it. Old MSI removal during an upgrade skips driver removal. A secure hidden
transaction GUID property selects a protected record; it cannot authorize one.
The preview ProductCode is development-only and must not be published.

```powershell
dotnet run --project Installer.Driver.Tests -c Release
dotnet run --project Installer -c Release -- --unified-preview `
  --staging C:\Code\HidHide\bin\Release\x64 `
  --driver-payload C:\Code\HidHide\artifacts\unified-driver-verified `
  --out C:\Code\HidHide\artifacts\unified-preview
.\build\TestUnifiedPreview.ps1 -Msi .\artifacts\unified-preview\HidHide.Unified.Preview.msi
```

## Historical development evidence

- 77 driver transaction checks: ordering, repair retention, filter repair,
  journal-write failure, every forward-step failure, reboot boundaries, exact
  identity rejection, unknown-outcome refusal and confirmed-resource rollback.
  These use the production engine with an in-memory Windows backend.
- 90 C++ tests, including real registry marker presence/read-error checks in an
  isolated per-user test key; no machine marker was created by those tests.
- 25 assertions over the actual MSI tables. They caught an initial directory-tree
  error, now fixed. Cabinet extraction confirmed all four driver/license hashes
  and inclusion of the worker in the custom-action cabinet. A modified license
  was rejected before package/output creation.
- Live read-only inspection confirmed `ROOT\SYSTEM\0004`, `oem34.inf`, driver
  1.4.181.0, expected service, control availability and all three filters on this host.

Logs and table exports are in `artifacts/unified-evidence/driver-*`,
`durable-barrier-build.log`, `unified-preview-*`.

Subsequent 2.0 host lifecycle testing is recorded in [native testing](native-lifecycle-testing.md).
Current implementation includes [missing-resource repair](unified-driver-repair.md)
and the [Burn controller](setup-controller.md). Final 2.1 native repair, migration,
upgrade and recovery validation remains open in the [release audit](release-readiness.md).

## API references

Implementation decisions were checked against Microsoft's documentation:
[SetupCopyOEMInfW](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupcopyoeminfw),
[UpdateDriverForPlugAndPlayDevicesW](https://learn.microsoft.com/en-us/windows/win32/api/newdev/nf-newdev-updatedriverforplugandplaydevicesw),
[DiUninstallDevice](https://learn.microsoft.com/en-us/windows/win32/api/newdev/nf-newdev-diuninstalldevice),
and [DiUninstallDriverW](https://learn.microsoft.com/en-us/windows/win32/api/newdev/nf-newdev-diuninstalldriverw).
