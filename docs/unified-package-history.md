# Unified package progress

**Native testing now underway; no preview is validated for use.** Actual install
testing reproduced null-HWND and invalid deferred SetMode failures. Both are fixed
in source. Failed uninstall recovery is complete. A fresh corrected install has
installed the applications and healthy driver, returned 3010, and retained its
restart/resume marker. Subsequent resume and repair passed; the GUI shutdown
double-destruction bug is fixed and its repair handoff was retested under CDB.
Ordinary uninstall/resume passed with strict empty-state verification. The package
containing the GUI fix passed clean install/resume, installed GUI navigation, and
same-artifact repair under CDB. Normal tray exit also passed under CDB; other release
gates below are still open. See
[native lifecycle testing](native-lifecycle-testing.md) before doing anything else.
Do not hand the user another installer to discover the next failure. Resume from
the documented restart checkpoint and complete the native lifecycle matrix first.

Current phase: 5–7 — Burn controller and offline runtime packaging implemented as a development preview; native lifecycle gates remain unverified.

Latest preview: `artifacts/unified-burn-v6/HidHide.Setup.exe` replaces v5.
Post-reboot upstream leftovers exposed two further preflight incompatibilities:
an empty REG_MULTI_SZ decoded as one empty string, and the exact pinned SYS left
without a service, device or package registration. Empty registry lists now decode
correctly; a fresh install may reuse only that exact inert SYS. Unknown bytes,
active ownership and malformed mixed filter lists still block. Uninstall's strict
empty-state requirement is unchanged. Existing bytes remain in the journal; an
inexact rollback still requires recovery rather than reporting success.
The actual `--validate-only` run passed ordinary-user handoff, elevated protected
cache verification and current driver/product policy on this host, then exited
before journal/marker creation, MSI planning or driver mutations. Evidence:
`artifacts/unified-evidence/burn-v6-validate.log`. Failures now include a fixed
phase plus exception type/HRESULT without configuration data. Tests: 31 installer,
109 driver and 77 controller checks pass. Full lifecycle gates below still apply.

Previous correction (v5):
After the user's upstream uninstall, Windows reported restart required and the
HidHide service had Start=4/DeleteFlag=1. The old preview hid controller failures
behind "Setup peer disconnected." The BA now detects pending service deletion
before elevation, and the controller sends fixed, non-sensitive failure responses
while its authenticated pipe is still open. 76 controller checks and 104 driver
checks pass. Read-only driver inspection confirms the explicit restart diagnostic.
No reboot or driver mutation was performed during this correction.
Branch: codex/unified-hidhide.
Source baseline: 4efeeabd038b2ff75929eed2df9c7cdf4b95947d (PR #20 included).
Changes are uncommitted; no merge or publication performed.

## Decisions and evidence
- Unified Burn bundle + private MSI; unchanged signed driver only. The state
  table, migration boundaries and stable new identities are in unified-package-design.md.
- Selected upstream EXE v1.5.230.0 is Advanced Installer, not Burn. Its actual
  driver is 1.4.181.0, dated 10/31/2023, AMD64 root\\HidHide.
- Exact hashes, catalog signer and licensing are pinned in build/driver-payload.json.
- Bundled nefcon 1.2.0.0 is excluded: source at 0c44775352584ef4b0f15d7a2e724036d843134b
  uses `reboot > 1` on BOOL and can lose required reboot state. Use owned API support.
- User authorized this host for lifecycle testing instead of a VM. No installer,
  driver, class-filter, trust-store or reboot mutation has been performed.
- Read-only native Windows Installer detection finds upstream 1.5.230 with
  ProductCode 01E0AB21-D1CC-42B4-9DFF-84FFE4F26DAF. No independent companion
  registration detected. All three applicable upper-filter lists contain HidHide.
- Existing untracked TrustTestCert.ps1, nefcon.zip, nefcon_out.txt and ignored
  artifacts preserved. None is used as trusted payload.

## Completed changes and checks
- Phase 1: read repository/current/historical installer implementations; fetched
  origin/feature/app-profiles and created requested branch. Concrete installation
  state table and recoverable failure outcomes written. MSI metadata queried
  read-only; upstream and companion 1.0/99 identities verified from actual packages.
- Phase 2: manifest, extraction-only acquisition, before/after staging hash,
  catalog trust/membership, INF identity and AMD64 PE/version verification implemented.
  Empty-cache acquisition passed. Twelve positive/negative checks passed, including
  a modified INF with a matching modified manifest hash rejected by catalog membership.
  Only INF/SYS/CAT and original license staged; no third-party helper redistributed.
- Phase 3: ProductVersion.props specifies 2.0.0.0. Both C++ projects consume it;
  installer validates both binaries against it and has no silent version fallback.
  Stable new bundle/MSI identities and conservative migration policy are defined.
  They are wired into the private MSI and Burn development preview; related-bundle upgrades remain gated.
- Release x64 Client, CLI and Tests rebuild succeeded using explicit VS18 compiler
  path. Both application versions verified 2.0.0.0. All 74 existing tests passed.
- Installer builds successfully. 28 version, migration-selection and filter-list
  checks passed. Actual read-only installed-product detector exercised successfully.
  These are not installer lifecycle tests or proof of implemented migration.
- One pre-existing MFC OnTimer annotation warning remains.

## Evidence
- artifacts/unified-evidence/version-build.log; artifacts/logs/x64/*.binlog;
  artifacts/tests/x64/results.xml
- artifacts/unified-evidence/acquisition.log and empty-cache.log
- artifacts/unified-evidence/payload-tests.log and installer-contract-tests.log
- artifacts/unified-evidence/upstream-inspection.json (read-only MSI table export)
- artifacts/unified-evidence/installed-products.log
- artifacts/unified-driver-verified and artifacts/unified-empty-cache-result

## Remaining blockers / unfinished work
- Native install/migration/repair/uninstall and reboot validation remain unperformed.
- Protected setup caching/journaling, legacy preparation, baseline restoration and
  conservative boot-verified resume are implemented; real power-loss/failure recovery
  and cross-user scenarios remain unverified.
- Earlier unified upgrades deliberately block pending related-bundle orchestration;
  same-version rebuilt bundle identity changes are not a validated repair path.
- Unknown MSI/native/legacy outcomes and damaged resources require explicit recovery.
  Automated legacy recovery UI and damaged-driver reconstruction remain unfinished.
- One Burn EXE with one private MSI builds, and its actual read-only smoke passes.
  Four app-local Microsoft runtime DLLs are packaged; clean/offline runtime testing,
  final UI/cancellation, release signing and unified CI/release replacement remain.
- No native lifecycle mutation or reboot was performed. Host testing cannot replace
  clean/offline and destructive fault-injection scenarios.

## Exact next action
Review and exercise the protected setup lifecycle under controlled native tests,
starting with preparation/cancellation and healthy repair, with verified recovery
sources available. Complete related-bundle upgrade handling, explicit incomplete-
transaction recovery and clean/offline/fault-injection coverage before release.
See [setup-controller.md](setup-controller.md) for implemented paths and exact gates.

Final checks in this work session:
- git diff --check passed.
- Installer tests rerun with Windows target: 28 passed without platform warnings.
- Rebuilt CLI read-only cloak/inverse queries succeeded against the installed
  driver (both reported off); no configuration changes requested.
- Installer rejected incomplete application staging before creating an output
  directory. Evidence: artifacts/unified-evidence/missing-application-rejection.log.

## Maintenance prerequisite completed (next-step implementation)

- Added authenticated payload-free preparation, baseline read-back and orderly
  coordinator shutdown. Persisted pause preference is preserved.
- Added machine-wide admission and a retained event barrier excluding new GUI/CLI
  writers. The ordinary-user CLI session holds ownership until explicit handoff.
- Preparation rejects unresolved recovery, owner refusal/timeout and changed
  snapshots. Controller protocol accepts only handoff/release; output stalls are
  cancelled and controller EOF releases the session.
- Live host checks passed for no-owner, active synthetic profiles and paused
  profiles; GUI/CLI exclusion and release passed. Original settings restored.
- This does not complete phase 6 or enable driver mutations. See
  [maintenance-session.md](maintenance-session.md) for protocol and remaining
  durable journal, worker, cross-user and reboot requirements.

Final maintenance verification:
- Release x64 Client, CLI and Tests rebuilt successfully; all 88 tests passed
  (74 existing plus 14 maintenance tests).
- Repeat live checks exposed transient admission contention during a profile tick.
  Added a 250 ms bounded admission wait; the final live suite passed.
- Controller EOF and invalid commands produced errors and released exclusion.
- No driver package, service, filter, trust-store or installer changes were made.
- Evidence: artifacts/unified-evidence/maintenance-build.log and maintenance-live.log.

## Driver backend and MSI preview implementation

- Implemented owned SetupAPI/NewDev backend, exact inventory, baseline backup,
  guarded filter changes, healthy/filter-only repair, and detach-before-delete.
- Added protected journal storage, recorded rollback/recovery boundaries, bounded
  worker execution and a persistent machine exclusion marker checked by GUI/CLI.
- Built the private MSI preview with checked deferred/rollback action ordering.
  Fixed a directory-tree error found by actual MSI table inspection.
- 77 driver transaction checks, 90 C++ tests and 25 MSI structure checks passed.
  Verified all four signed driver/license payload hashes inside the MSI and the
  embedded worker. Native read-only detection matches this host's installed driver.
- No lifecycle mutation or reboot performed. The preview requires a protected
  transaction from the unfinished controller and is not an installable release.
- Details, failure boundaries and source references: [driver-lifecycle.md](driver-lifecycle.md).


Final preview checks: all four packaged driver/license hashes match the manifest;
a modified license was rejected before output creation. Evidence is in
artifacts/unified-evidence/unified-preview-payload.log and unified-tampered-license.log.
The final native inspection still reports a healthy existing driver; no machine
maintenance marker or running test manager was left behind.

## Burn controller development preview

- Implemented ordinary-user READY handoff, PID/SID-authenticated elevated controller,
  protected embedded cache/setup journal, exact legacy migration outside MSI,
  checked Burn MSI handoff, boot-verified checkpoints, baseline/final byte verification
  and matching-marker cleanup. Native deadlines retain exclusion on unknown outcomes.
- Added four pinned signed app-local Microsoft runtime DLLs to the private MSI and
  three CLI dependencies to the bootstrapper, avoiding a second chained installer.
- 74 controller checks and 104 driver/settings checks pass. Actual bundle smoke
  loads the WiX5 BA and extracted controller, performs read-only detection and exits0;
  no preparation, elevation, planning or application is requested in that mode.
- A real smoke exposed an STA/COM bootstrapper entry-point failure; removing the
  inappropriate STA annotation fixed it. The final bundle was rebuilt and retested.
- Current unsigned artifact: artifacts/unified-burn-v4/HidHide.Setup.exe.
  Evidence: controller-tests.log, burn-build-v4.log, bootstrapper-build-final.log,
  burn-inspect-only-final.log under artifacts/unified-evidence.
- Native lifecycle validation and the gates above remain open. Implementation details
  and recovery limits are documented in [setup-controller.md](setup-controller.md).
