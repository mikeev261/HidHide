# MSI failure recovery

Implemented bounded retry; actual Windows Installer failure-injection coverage
remains required before release.

A terminal failed Burn Apply is recorded separately from an interrupted MSI
transaction whose outcome was never received. The former can be inspected when
the user reruns the same setup and chooses Resume. It is never converted directly
to success. The latter remains fail-closed in MsiPending.

Before MSI execution, setup records hashes or confirmed absence for its ten
fixed application/runtime/driver-payload files and its Start-menu shortcut. It
also records the pre-MSI ProductCode and exact related Burn bundle identities,
versions and cached-EXE hashes from both machine registry views. No display-name
matching or uninstall registry mutation is used.

Retry requires all of the following:

- The terminal failed Apply was durably reported; legacy removals are complete.
- Full protected replacement/recovery sources still verify.
- The original MSI product inventory, owned files and related Burn registrations
  are restored exactly.
- The protected driver journal is either untouched Prepared with no steps, or
  RolledBack with every step completed and undone. No reboot is pending.
- Observed native resources and baseline equal the journal's Before snapshot.

The complete previous native attempt is archived under a fixed protected
transaction/attempt filename before the retry phase is saved. An interrupted
archive write cannot be overwritten or treated as complete. A complete archive
followed by a crash is accepted again only when its bytes match the same source
journal. Preparation rechecks native state before creating a fresh attempt.
The maintenance marker, confirmed baseline and completed migration history stay
in place. At most 32 verified retries are permitted.

Pre-Apply cancellation has a separate path: the controller independently proves
the native journal is still untouched before returning to the prepared phase.
It does not rely on the failed-Apply rollback path.

The controller tests exercise retry versus success, failed proof retaining the
original attempt, native status/undo/reboot exclusions and changed evidence.
Read-only execution on the development host confirmed snapshot collection from
the real owned paths and Burn registration. Those checks do not replace actual
failed-upgrade and rollback testing in Windows Installer.

## Remaining uncertain outcomes

A known completed native rollback prefix can continue across reboot through the
explicit [rollback direction protocol](unified-driver-rollback-reboot.md). Setup
first verifies that Windows Installer restored files and registration. Each
additional reboot retains the terminal failed Apply and prevents another MSI
attempt until exact native rollback is proven. Interrupted inverse calls remain
blocked; restarting does not convert unknown intent into completion.

Interrupted legacy uninstallation is also not inferred complete from missing
ProductCode registration. The current legacy intent does not retain sufficient
original-process and complete legacy-resource evidence for that inference.
These cases require a separate verified recovery protocol and integration tests;
they are not counted as supported automatic recovery by this implementation.
