# Maintenance preparation interface

This is the live user-mode prerequisite for unified setup, not an installer or a
durable migration journal. Kernel code and exported IOCTLs are unchanged.

## Admission and restoration

New GUI instances, direct CLI transactions and resident-manager edits/reconciliation
acquire `Global\HidHide.AppProfiles.Admission.v1` before proceeding, with a 250 ms wait for routine contention. They reject
access while `Global\HidHide.AppProfiles.Maintenance.v1` exists. The barrier is an
event retained by open handles; signalling or resetting it cannot admit writers.

The ordinary configuration user starts `HidHideCLI.exe --maintenance-session` with
redirected stdin/stdout pipes. Elevated invocation is refused. The session creates
the barrier and acquires the existing coordinator mutex. If a manager owns that
mutex, it sends protocol-v1 command 3, PrepareMaintenance, with no payload. The
existing pipe authenticates both peers as the same user. Unsupported owners,
unreachable owners and authentication failures refuse preparation; there is no
fallback to direct mutation.

The manager observes fresh state, refuses ownership conflicts, restores its baseline
and confirms it by reading back. It preserves the persisted pause preference,
finishes delivering the reply, then exits. The session waits at most five seconds
for ownership, refuses any remaining initiating-user recovery key, and compares a
fresh snapshot with the confirmed baseline. New normal writers remain excluded.
If no owner exists, unresolved initiating-user recovery also blocks preparation.
A missing driver is represented explicitly, with the user's profiles retained. If its control interface is unavailable, strict typed service Parameters may provide a confirmed stored baseline for recognized repair. The elevated worker independently checks it; see [repair](unified-driver-repair.md).

## Controller protocol

On success stdout emits one line, `READY <uppercase hex>`. Decoded bytes use the
existing bounded little-endian protocol encoding, in this order:

1. Setup protocol version (uint32, currently 2; configuration IPC remains version 1).
2. Initiating user SID (length in UTF-16 code units, followed by each code unit encoded as uint32, matching `Protocol::Writer::String`).
3. Live control-interface availability Boolean (uint32, 0 or 1).
4. Confirmed-baseline availability Boolean (uint32, 0 or 1).
5. Configuration state (active, inverse, blacklist, whitelist and user profiles).

Treat the snapshot as sensitive configuration; do not copy it to ordinary logs.
The controller must drain stdout and stderr and treat any nonzero exit, malformed
reply, disconnect or timeout as failure. Each stdout write has a five-second wait
before cancellation; cancellation completes before its buffer is destroyed.
Input lifetime is 30 minutes. Only these newline-terminated commands are accepted:

- `handoff`: releases the session's coordinator mutex but retains its barrier
  handle; replies `HANDED_OFF`. A second handoff is an error.
- `release`: exits successfully, releasing the remaining handles.

CRLF is accepted. Oversized input, arbitrary commands, paths and extra arguments
are rejected. Controller EOF releases the session with an error. A successfully
prepared manager remains exited if subsequent setup is cancelled; restarting it is
an explicit controller responsibility after maintenance ends.

## Installer integration contract

Before sending `handoff`, the elevated worker must open its own barrier handle.
It must acquire the coordinator mutex after handoff, revalidate actual state and
identity, and create the protected durable journal before changing the driver.
Keep its barrier handle and ownership until the transaction or recovery finishes.
The event is cooperative exclusion, not authentication of an elevated operation.
The READY data must never authorize arbitrary privileged registry/file writes.

The worker, driver-step journal, durable marker and guarded MSI hooks are now implemented in [driver-lifecycle.md](driver-lifecycle.md). Burn preparation/completion and conservative reboot checkpoints are implemented in [setup-controller.md](setup-controller.md). Cross-user recovery and native lifecycle validation remain gated.
The live event disappears when all handles close. The driver worker now adds a protected machine marker for restart exclusion; see [driver-lifecycle.md](driver-lifecycle.md). Historical clients do not honor the new barrier.
Consequently this interface alone must not enable driver install/remove actions.

## Verification

Release x64 unit tests exercise actual admission/event/mutex primitives, production
request parsing, ownership timeout/refusal, baseline mismatch, retained handoff,
recovery rejection, fixed controls and stalled/disconnected stdout cancellation.

Live Windows 11 x64 checks used synthetic device IDs only. They verified no-owner
preparation, GUI/CLI exclusion, release, active-profile baseline restoration,
orderly manager exit and preservation of both paused and unpaused preferences.
Controller EOF and invalid-command checks verified failure exits and barrier release. Original driver baseline, profile storage, pause preference and autostart were
restored. No driver package, service, filter or installation changes were made.
Cross-user and reboot/fault-injection lifecycle scenarios remain unverified.

Evidence: `artifacts/unified-evidence/maintenance-build.log`,
`maintenance-live.log`, and `maintenance-live.ps1`.
