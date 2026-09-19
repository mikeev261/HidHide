> Product decision (2026-09-07): companion-only packaging restrictions below are superseded by the [unified package design](docs/unified-package-design.md). The target is one HidHide Profiles Burn setup with a private MSI owning the applications and an unchanged verified Microsoft-signed driver. Kernel changes, unsigned driver builds and test-signing are excluded. Implementation status and open gates are recorded in [progress](docs/unified-package-progress.md). Existing companion build commands below remain historical until the unified build is implemented.

# App Profiles reliability and release plan

Prepared: 2026-09-07. Review baseline: `e1263ae` on `codex/app-profiles-redesign`.

Deliver a dependable App Profiles companion for the separately installed Microsoft-signed HidHide driver. Concentrate the next milestone on correctness, configuration ownership, recovery, and release readiness. Each work package below is sized around a reviewable change and has an explicit completion gate.

All implementation work below is planned. Creating this document does not mark any implementation or release validation complete.

## Starting evidence

- The x64 Release GUI, CLI, and test runner were rebuilt during the review; all 11 existing tests passed. The GUI build reported an MFC annotation warning in `BlacklistDlg.cpp`.
- Existing tests cover CLI parsing and IOCTL constants. Profile transitions, persistence, recovery, and installer behavior lack behavioral coverage.
- `HidHide.sln` has test configuration entries but no test project declaration; the NUKE test target expects a runner that the solution does not build.
- Source inspection identified baseline selection loss during overlapping profile edits, lifetime ownership of the exclusive control handle, and cached state changes before successful driver writes.
- Startup timing, ARM64 runtime behavior, and installer lifecycle were not exercised. Treat these as validation work, not confirmed passing or failing behavior.

## Product contract for this milestone

Use these defaults when implementing the plan, and document any change before building dependent behavior:

- Profiles activate the union of their device selections on the driver's global hidden-device list. They do not provide simultaneous, independent visibility rules for different applications. Normal whitelist and inverse-whitelist behavior still applies.
- Store the user's baseline separately from profile contributions. Companion GUI and CLI edits to normal device settings must express baseline intent directly; never reconstruct that intent by subtracting profile selections from an effective list.
- Preserve activation when the saved baseline has hiding disabled: a running profile may temporarily enable hiding, and ending the override restores the saved baseline state. However, an explicit user action to disable hiding while profiles are active must suspend automatic overrides until the user resumes them. The manager must not silently re-enable hiding on the next scan.
- Pausing automatic profiles restores the baseline; resuming recomputes the active profile union. Exiting restores the baseline when the manager still owns the configuration. Unresolved external changes require conflict handling before restoration.
- Only one coordinator may own global profile overrides across Windows sessions. Profile definitions remain per-user. Cross-user takeover must not restore recovery data belonging to an unrelated user.
- Automatic detection after an application starts is best effort until launch-timing tests establish the supported behavior. Do not promise that changing the list revokes already-open device handles.
- Keep the companion payload limited to user-mode components. Kernel enforcement expansion, a UI framework migration, and a general driver rewrite are outside this milestone.

## Execution order

| Package | Outcome | Dependencies |
|---|---|---|
| 01 | Clean builds produce and run the intended tests | None |
| 02 | Configuration ownership design is demonstrated | 01 |
| 03 | Baseline and profile transitions are independently tested | 02 |
| 04 | Writes, persistence, and recovery survive failures | 03 |
| 05 | GUI, CLI, monitoring, and lifecycle use the same rules | 02, 03, 04 |
| 06 | Companion packaging and release tooling are consistent | 01; lifecycle integration requires 05 |
| 07 | Windows integration matrix establishes release readiness | 05, 06 |
| 08 | A release candidate is assembled from verified evidence | 07 |

Follow this order for implementation. Packaging cleanup can be developed independently after package 01, but installer shutdown and recovery behavior depends on the completed coordinator. Keep behavior changes and unrelated formatting changes in separate reviews.

## 01 — Restore a trustworthy build and test pipeline

Primary files: `HidHide.sln`, `HidHide.Tests/HidHide.Tests.vcxproj`, `build/Build.cs`, `appveyor.yml`, `BUILD_AND_RELEASE.md`.

- Restore the test project declaration and correct its x64/ARM64 configuration mappings; remove orphaned solution entries after checking their purpose.
- Make the companion build explicitly build GUI, CLI, and tests. Separate optional kernel/CAB work from the default companion release path while retaining a documented driver-development entrypoint.
- Ensure CI validates the development branch and pull requests, rather than relying only on master/tag builds. Publish test results and useful build logs.
- Record required compiler, SDK, MFC, .NET, and WiX versions from working configurations. Keep environment-specific workarounds separate from product fixes.
- Establish clean-checkout build commands with no dependency on existing `bin`, `obj`, staging, or installer artifacts. Run this verification in an isolated checkout or disposable environment.

Completion gate: a clean x64 Release build produces and executes all existing tests; ARM64 companion components and tests compile; a deliberately failing test causes the CI job to fail. ARM64 compilation is recorded separately from ARM64 execution.

## 02 — Demonstrate configuration ownership before a larger refactor

Primary files: `HidHideCLI/src/FilterDriverProxy.*`, `HidHideClient/src/ProfileManager.*`, `HidHideClient/src/HidHideClientDlg.*`, `HidHideClient/src/HidHideClient.cpp`.

- Write a short architecture decision describing the owner of baseline settings, effective settings, profile definitions, recovery records, and driver transactions.
- Preferred design: one coordinator serializes companion GUI/CLI commands and owns override state; acquire the driver's exclusive handle only for bounded read/modify/write operations. Use fresh driver snapshots at transaction boundaries.
- Demonstrate a minimal local command path for the CLI when the coordinator is running, plus a coherent direct/offline path when it is absent. Profile storage operations must not require a permanently open driver handle.
- Support temporary contention from the official configuration utility with bounded retries and visible status. Detect unexpected driver state and suspend reconciliation pending explicit conflict resolution; do not overwrite external edits or restore an obsolete baseline automatically.
- Prototype cross-session ownership, standard-user permissions, command authentication, orderly handoff, and abandoned-owner handling. Keep IPC limited to configuration commands; it must not become an arbitrary command executor.
- Prove the design against the supported signed driver before committing to the IPC and persistence implementation. Document any third-party interoperability limitation that cannot be resolved through user-mode changes.

Completion gate: the manager can remain resident while the companion CLI works; another configuration client can acquire the driver between transactions; two Windows sessions cannot independently apply overrides; all contention/conflict paths preserve settings and give a useful status.

## 03 — Extract and fix profile state transitions

Primary files: `HidHideClient/src/ProfileManager.*`, `HidHideClient/src/BlacklistDlg.*`, `HidHideClient/src/AppProfilesDlg.*`, `HidHide.Tests/`.

- Extract the policy/state-transition logic into a small user-mode component that does not require MFC, a real registry, process enumeration, or an installed driver to test.
- Keep adapters for process observations, configuration storage, and driver access narrow. Avoid a broad framework or unrelated reorganization of shared CLI sources.
- Represent baseline selections, baseline enabled state, profile contributions, override ownership, and suspension explicitly.
- Fix the concrete regression: baseline `{A}`, active profile `{A,B}`, and a baseline edit adding `C` must restore `{A,C}` when the profile ends.
- Tag scan results with the configuration revision they describe and reject stale results at consumption. Distinguish a failed/incomplete process scan from a successful scan finding no active profiles.
- Specify behavior for empty profiles, multiple instances of an executable, profile deletion while active, overlapping profiles, rapid starts/exits, inverse whitelist mode, and device selections excluded by UI filters.

Completion gate: deterministic tests assert externally meaningful outcomes for those transitions, including pause/resume, explicit disabling, baseline edits to overlapping devices, stale results, and scan failures. The GUI consumes the tested policy rather than maintaining a second implementation.

## 04 — Make writes and recovery dependable

Primary files: `HidHideCLI/src/FilterDriverProxy.*`, `HidHideClient/src/ProfileManager.*`, the storage adapters introduced in package 03, `HidHide.Tests/`.

- Update cached applied state only after successful driver writes. Distinguish desired state, confirmed state, and unknown/partially applied state so failed operations remain retryable.
- Define the ordering and recovery behavior for multi-operation changes; do not assume separate IOCTLs are atomic. Read back after ambiguous outcomes.
- Replace delete-all/rewrite profile persistence with a recoverable commit strategy. Preserve the last valid profile set if a save is interrupted, and serialize cooperating writers.
- Version profile and recovery data. Migrate existing records without losing selections, and handle malformed, incomplete, unsupported-version, and oversized records explicitly.
- Persist a complete recovery record before applying an override. Include ownership and enough baseline/expected-state information to detect conflicts. Clear the record only after confirmed restoration.
- Define recovery guarantees for process crashes versus system/power interruption, including durability and writes interrupted before their completion marker. Ensure a failed journal write prevents the override.
- Provide a way to surface and resolve recovery conflicts. Never silently report successful restoration when writes failed.

Completion gate: fault-injection tests interrupt every persistence/driver-write boundary, restart the coordinator, and verify either correct restoration or an explicit conflict/error with recovery data retained. Failed writes must be retryable; unsuccessful saves must retain the prior valid configuration.

## 05 — Finish resident operation and user controls

Primary files: `HidHideClient/src/ProfileManager.*`, `HidHideClient/src/HidHideClientDlg.*`, `HidHideClient/src/AppProfilesDlg.*`, `HidHideClient/HidHideClient.rc`, `HidHideCLI/src/Commands.cpp`, `README.md`.

- Integrate coordinator commands into both GUI and CLI. Refresh configuration changes between clients and return nonzero CLI exit codes for failures or rejected commands.
- Add pause/resume and distinguish configured, detected, applied, suspended, contended, and failed states. Show which profiles contribute to effective hiding and make global scope clear in the UI.
- Keep process enumeration and slow driver/storage work off the UI thread. Surface worker failure and recover it or enter a visible stopped state; avoid silently retaining an apparently healthy status forever.
- Validate exact executable identity, duplicate filenames, inaccessible processes, volume/path changes, and multiple process instances. Any filename fallback must be explicit in status and tested against false activation.
- Refresh device availability on hotplug. Preserve filtered/disconnected selections and show accurate counts; retain composite-device expansion behavior.
- Define autostart behavior for GUI and CLI profile changes. Handle no profiles, sign-out, restart, Explorer restart, missing driver, and the official driver being upgraded or removed.
- Add bounded diagnostic events for profile transitions, write failures, conflicts, and recovery; avoid flooding logs on every polling interval. Update resources and documentation with the implemented behavior.

Completion gate: GUI and CLI operations agree while the manager is resident; explicit disable/pause remains effective; lifecycle and error paths have usable status and no silent loss of baseline settings.

## 06 — Align packaging and release tooling with the companion

Primary files: `Installer/Program.cs`, `build/Build.cs`, `Build_MSI.cmd`, `release.ps1`, `appveyor.yml`, `.github/workflows/winget.yml`, `BUILD_AND_RELEASE.md`, `INSTALL_LAYOUT.md`.

- Remove unreachable driver test-signing/download machinery from the companion path after checking references. Keep optional driver tooling clearly separated.
- Remove hard-coded workspace paths and stale artifact names. Make repeated staging/MSI builds deterministic even when an output directory already contains a previous installer.
- Disable the inherited publication path targeting `Nefarius.HidHide`; configure a distinct companion package identity only when ready to publish it. Make artifact account/project selection explicit rather than downloading upstream builds by default.
- Preserve the independent companion MSI upgrade identity. Inspect generated MSI tables/payloads to confirm no driver package, class-filter action, or official-package removal is introduced.
- Define the supported-driver prerequisite and useful behavior when it is missing. Keep installation compatible with the standard-user runtime contract.
- Integrate orderly coordinator shutdown/restoration into upgrade and uninstall. Account for runtime-created autostart entries, per-user configuration, and recovery records across users; do not remove the only recovery record before restoration succeeds.
- When signing is configured, sign user-mode payloads before MSI packaging, then sign and verify the MSI. Use the maintainer's configured identity; do not assume access to upstream signing credentials.
- Audit release-script path handling before cleanup and avoid command-string evaluation where structured invocation works. Record the source commit and artifact hashes with release output.

Completion gate: clean and repeated builds produce only the expected companion payload; install/upgrade/repair/uninstall leave the official driver installation intact; stale autostart and override state are handled; publication tooling cannot accidentally target the upstream product.

## 07 — Run the Windows integration matrix

Use disposable Windows environments for installer changes, forced termination, and recovery testing. Exercise the supported signed driver with Secure Boot enabled. Record OS, architecture, driver version, source commit, device identifiers, observed outcome, and logs for each scenario.

| Area | Required scenarios | Passing result |
|---|---|---|
| Profile behavior | One profile; overlapping profiles; duplicate process instances; active profile edits/deletion; baseline edits; inverse whitelist | Effective hiding and restoration match the documented policy |
| Launch timing | A small probe opens HID immediately and retains its handle; representative supported games/launchers start normally | Detection/apply timing and actual handle access are measured; limitations are reproducible |
| Ownership | Resident manager plus CLI; official utility; two user sessions; contention; owner crash | No competing coordinators or silent overwrite of user settings |
| Recovery | Forced manager termination; failed writes; sign-out/restart; incomplete/malformed journals; external edits after a crash | Baseline restored when ownership is valid, otherwise a visible recoverable conflict |
| Devices | USB/Bluetooth where supported; reconnect; composite HID/XUSB; duplicate friendly names; disconnected/filtered entries | Correct device identity and selections survive transitions |
| Lifecycle | No profiles; CLI-created profiles; autostart; Explorer restart; missing/updated driver; install/upgrade/repair/uninstall | Documented behavior without stale active overrides or misleading status |
| Responsiveness | Idle polling, many configured profiles, slow device enumeration, repeated process churn | UI remains responsive, worker failures are visible, resource usage stays bounded |
| Architecture | x64 execution; ARM64 compilation and execution on a suitable host | Each advertised architecture has explicit build and runtime evidence |

Launch-timing decision: if supported applications acquire devices before automatic hiding applies, make a reliable pre-activation/launch workflow a prerequisite for claiming support for those applications. Faster polling alone is not proof of correctness. A controlled launch must confirm application of the profile before starting the target and retain activation during the launch handoff; failed launches must restore settings.

Completion gate: every required scenario for the advertised support matrix has recorded results. Fix reproducible correctness failures and add regression coverage where practical. Mark unavailable hardware/environment checks as unverified; withhold unsupported architecture/application claims rather than treating compilation as runtime validation.

## 08 — Assemble the release candidate

- Run the targeted audit again over the final changes: configuration ownership, state transitions, registry parsing, IOCTL results, handle lifetime, worker shutdown, IPC validation, and installer/release identity.
- Resolve findings and repeat only the affected tests plus required release checks. Keep unrelated cleanup in follow-up work.
- Produce release artifacts from a clean checkout and attach test results, integration evidence, hashes, supported OS/driver/architecture combinations, upgrade/uninstall instructions, and known limitations.
- Update the end-user README to explain global profile effects, whitelist interactions, autostart, pause/resume, recovery, CLI usage, and launch-timing limitations.
- Prepare the release candidate for review. Publishing and package-manager submission are separate actions from assembling and validating the candidate.

Completion gate: no unresolved baseline-loss, unrecoverable-override, ownership, installer-identity, or supported-application correctness defect; all advertised configurations have evidence; clean-build artifacts match the reviewed commit.

## Expansion after the reliability milestone

Prioritize controlled launch if package 07 demonstrates that supported applications need it. Otherwise choose the next addition from observed user needs:

1. **Launch with profile:** apply and confirm settings before launching, cover delayed launchers and launch failure, and restore safely. Reuse the tested coordinator.
2. **Explain effective hiding:** show the baseline and profile contributions responsible for each hidden device, with actionable conflict/recovery diagnostics.
3. **Profile import/export:** use the versioned configuration format, validate executable/device mappings on the destination machine, preview changes, and apply through the same commit path.

True simultaneous per-application isolation requires a separate design and a viable driver-signing/distribution path. Broad modernization of the kernel or MFC UI should have its own measurable objective after the companion's reliability gates are met.

## Execution evidence — 2026-09-07

Package 01 is implemented in the working tree but its completion gate is **open**.
Packages 02–08 have not started because the plan requires package 01 first.

- Restored the test project declaration and all test configuration mappings;
  removed orphaned mappings belonging to the absent Watchdog solution project.
- Companion NUKE compilation explicitly rebuilds GUI, CLI, and tests. Default
  build runs tests; CI adds MSI; driver/CAB targets are opt-in.
- CI branch filtering removed; explicit exit-code checks, Google Test XML upload,
  and compiler binary-log artifacts added. Inherited automatic deployment removed.
- Isolated source snapshot (tracked HEAD plus package 01 changes; no copied
  output/package directories) rebuilt x64 Release successfully, passing all 11
  tests. Evidence: `artifacts/package01-clean-x64.log` and snapshot binary logs.
- Injected a twelfth, deliberately failing test only into the disposable snapshot.
  `UnitTest` failed and `build.ps1` returned a nonzero exit code (-1; the test runner returned 1). Evidence:
  `artifacts/package01-deliberate-failure.log`. This proves local entrypoint
  propagation; an actual AppVeyor job has not been run.
- ARM64 compilation attempted in the same snapshot and failed before compiling:
  MSB4086, empty PlatformToolsetVersion. The installed VS 18 compiler has only
  Hostx64/x64 and Hostx64/x86 binaries and no ARM64 v145 platform toolset.
  Evidence: `artifacts/package01-clean-arm64.log`. Install the matching ARM64
  MSVC/MFC components or use a provisioned build host, then repeat compilation.
- Existing MFC annotation warning remains. ARM64 execution, installer lifecycle,
  signed-driver ownership demonstrations, and hosted CI results are unverified.

Next required work: provision ARM64 build tools and run both architecture jobs
in CI, including the deliberate-failure gate in a disposable CI branch. Only
then proceed to package 02's signed-driver ownership proof.

## Scope correction — Windows 11 x64 personal use

The user confirmed that only Windows 11 x86_64 is intended. This supersedes
ARM64 and public-release requirements above for the current execution:

- ARM64 compiler installation, compilation, and runtime validation are out of
  scope and do not block any subsequent package.
- Local clean x64 builds and failure-propagation checks satisfy the current
  package 01 gate. Hosted CI execution is deferred release work, not a blocker
  for this personal-use implementation.
- Package 02 may proceed. Keep driver transactions and recovery safe across
  processes and Windows sessions even on a single x64 machine.
- Public release candidates, package-manager publication, and a multi-architecture
  support matrix are deferred. Retain x64 correctness and recovery validation.

Package 02 source inspection confirms the proxy holds its driver handle for its
entire lifetime. Its setters also update cached state before driver writes
succeed. The next implementation must coordinate GUI/CLI commands and compare
fresh driver state before writes; merely shortening handle lifetime would permit
stale cached settings to overwrite another client's edits.

## Package 02 implementation — 2026-09-07

Implemented the x64 coordinator, bounded driver transactions, same-user local
configuration protocol, fresh expected-state checks, explicit baseline intent,
persistent suspension, and conflict/recovery status. See
[CONFIGURATION_OWNERSHIP.md](CONFIGURATION_OWNERSHIP.md) for the ownership decision,
protocol/permissions, storage changes, and verification limits.

All 19 unit/Windows IPC tests passed. Live signed-driver checks passed for resident
CLI access, independent handle acquisition, bounded contention, second-owner
rejection, the A/A+B/add-C overlap regression, persistent explicit disable,
external-edit conflict retention, and owner-crash baseline recovery. Tests used
nonexistent device IDs and restored original settings afterward. Cross-session and
cross-user runtime checks remain unverified, as do exhaustive persistence/IOCTL
fault injection and installer lifecycle. Do not mark those release gates complete.
