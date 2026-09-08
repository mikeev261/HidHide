# Native rollback across reboot

The native journal distinguishes forward `RebootRequired` from explicit
`RollbackRebootRequired`. Existing records do not acquire rollback direction by
inference. The MSI rollback callback can request rollback of a completed forward
prefix; if that prefix already requires restart, it records direction and waits
without undoing resources on the current boot.

Each inverse step durably records `UndoStarted` before the native operation and
`Undone` after its acknowledged return. An interrupted inverse intent is never
replayed. After a completed operation requests restart, continuation requires a
different validated boot identity and independently verifies the remaining exact
node, published INF, class filters and baseline. Filter reversal requires a boot
before deleting a newly created node. Node and package removal may each require
another boot. Only a newly introduced, pinned SYS can receive final cleanup,
after all ownership disappears. Completion requires the exact original snapshot.

Supported undo is fresh installation or filter-only repair/upgrade of retained
healthy resources. Reconstructive repair after binding and uninstall after
removing prior resources require explicit restoration, not deletion of the
repaired driver or guessing prior ownership. If Windows removes an original
orphan SYS while uninstalling its new package, original-state verification fails;
this path does not fabricate a successful restore of that original file.

Tests exercise multiple reboot barriers, external changes, retained baseline,
native inverse failure, and reloading the last durable journal after an inverse
completion write failure. They do not replace the disposable Windows lifecycle
and forced-failure matrix. The controller verifies terminal failed MSI Apply and
restored MSI files/product/Burn inventory before continuing native rollback. It
retains the failed-Apply record through additional rollback reboots; only complete
rollback allows archiving and a new MSI attempt. A completed old forward reboot
prefix can be explicitly converted only after these failed-Apply checks.
