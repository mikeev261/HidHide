# Configuration state and ownership (issue #7)

## Investigation and plan

The old proxy retained construction-time driver/profile values and changed them before attempting writes. Its lifetime also retained the control handle even though the existing signed driver declares the control device exclusive. Sharing flags alone cannot enable another CLI instance to open that device.

The implementation separates an injected backend from an explicit live or snapshot configuration session. The production backend holds a short-lived exclusive control handle and a cooperating-client global mutex through each read/check/write operation. Live getters read current state; CLI snapshots stage a batch, preflight changed fields, and commit only those fields. Successful fields are acknowledged individually. Profiles retain the existing per-user registry schema and use a registry transaction for atomic replacement; read errors are reported instead of interpreted as empty profiles.

A profile manager owns global driver state through a machine-wide lifetime mutex. Its override writes compare the explicit expected blacklist/active pair under one exclusive driver handle, independently of ordinary live getters. Recovery callbacks record only successfully persisted fields, and failed recovery writes remain retryable. Registry recovery records are transactional and include the expected effective pair. Matching records restore the baseline; external changes or legacy records without sufficient evidence preserve the current configuration and pause profiles.

## User-visible behavior

CLI profile edits are observed by a running manager on its next polling tick. CLI batch edits remain local until commit; an external edit to a changed field raises a conflict rather than being overwritten. Unchanged fields are never written back by a snapshot.

While a profile override is active, driver changes from another tool or the Devices tab pause automatic profiles. A message and tray tooltip report the conflict. Review the preserved configuration in HidHide, close the application, and restart it to adopt that configuration as the new baseline and resume profiles. The conflicting recovery marker is discarded; no automatic stale baseline restoration occurs on exit. Another Windows session cannot run a concurrent profile manager; a denied/busy ownership lock produces an explanatory message.

## Limits

There is no atomic transaction spanning driver IOCTLs and registry updates. A successful prefix of a batch remains persisted when a later write fails, and only unfinished fields retry. A crash between a driver write and its recovery-record update may require the explicit conflict/review/restart path. Legacy recovery records also require this path because they lack expected-state evidence.

The global mutex coordinates updated clients; the exclusive driver handle prevents other control-device callers from changing driver state during a conditional operation. Direct registry editing and old clients do not follow the mutex protocol. Registry transactions prevent partial map publication, but do not provide a global revision/CAS protocol for arbitrary noncooperating registry writers. Global-lock ACL failures are surfaced rather than bypassed. Registry transactions require Windows KTM support; transaction failures are reported and are retryable, with no destructive fallback.

Profile baseline-overlap policy (#6) and scan/activation revision races (#5) remain separate work. This change conservatively pauses on baseline edits during an active override instead of attempting an unsafe merge. No driver API, driver binary, installation, or live driver/registry configuration was changed during validation.

## Validation

Release x64 builds of HidHideCLI, HidHideClient, and HidHide.Tests succeeded using Visual Studio 18 MSBuild. All 27 tests passed: 14 injected-backend configuration regressions, 2 named-ownership mutex tests, and 11 existing parsing/ABI tests. The regression cases cover live visibility, deferred batching, dirty-only writes, stale edits, preflight conflicts, partial commit retries, failed profile writes, expected-pair interleavings, recovery callback failures, exclusive lease duration, competing sessions, and abandoned ownership.

The client build reports the existing C28251 BlacklistDlg annotation warning. No live GUI/driver configuration smoke test or ARM64 build was performed. Transactional Win32 registry adapter behavior is compiled but its OS-level fault paths are not exercised by the injected-backend tests.

GUI views keep their own expected configuration values, independent of shared live
getter caches. Profile edits therefore conflict if a CLI edit or deletion occurs
after the tree was displayed, even when a status poll has read the newer values.
Filtered profile edits retain paths outside the displayed controls. Devices and
Applications likewise condition whole-list saves on their displayed snapshots.
Conflicts are reported and reload the view; operational failures are reported
without ending the application. Profile polling retries reads and pending saves,
using the same explicit expectation, while preserving pending selections.

Review-fix validation: Release x64 Client, CLI and Tests projects built with Visual
Studio 2026 MSBuild. All 32 tests passed, including stale displayed profile edits,
external deletion, filtered selection retention, getter interleaving and exclusive
contention/retry. Tests use an in-memory backend; no installed driver or actual
configuration registry values were changed. Interactive GUI contention was not
exercised against a live driver.

Final GUI review fixes: profile application lists, device trees, and interface
counts now render one profile-map snapshot. Selection/filter refreshes rebuild
both controls, so they cannot acknowledge CLI additions/deletions absent from the
application list. Failed Enable/Inverse saves restore the acknowledged checkbox
value immediately. Failed device-tree saves restore the last acknowledged checks
and icons without reading the backend, allowing the same click to retry even
through persistent backend failures. Profile pending-save retry behavior remains
unchanged.

After these fixes, Release x64 Client, CLI, and Tests builds succeeded and all 32
tests passed again; `git diff --check` passed. The existing C28251 warning remains.
The new MFC rendering/rollback paths were inspected and compiled, but are not
exercised by the backend-only test harness. No GUI fault injection or live
configuration mutation was performed. Manual follow-up should inject persistent
write/read failures while toggling Enable, Inverse, and parent/child device checks,
and interleave CLI profile additions/deletions with GUI selection/filter changes.

Selection-refresh follow-up: every profile view refresh now marks itself pending
before reading configuration, and acknowledges completion only after the complete
list/tree/status refresh succeeds. Operational failures during selection, filter,
show, add/delete/drop, or timer refresh therefore retry even if the profile map
has not changed. Timer retries refresh before considering a pending tree save;
the selected/displayed-profile guard and expected-map comparison remain in place.
This retry performs reads/rendering only and does not replay add/delete writes.

Release x64 Client, CLI, and Tests builds succeeded after this follow-up, all 32
existing tests passed, and `git diff --check` passed. The existing C28251 warning
remains. No new automated UI regression was added: the current harness does not
instantiate MFC dialogs or inject their driver proxy, and a separate boolean
simulation would not exercise this defect. The selection A-to-B/read-failure/
unchanged-map retry transition was inspected in the actual dialog control flow;
interactive fault injection, including verifying no writes and unchanged backend
data during refresh failure, remains unperformed. No live configuration was used.
