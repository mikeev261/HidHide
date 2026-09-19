# HidHide Profiles: profiles-first implementation plan

Status: authorized implementation contract for Windows 11 x64. This replaces the
current App Profiles interaction and registry profile catalog. It does not replace,
modify, rebuild, or re-sign the existing Microsoft-signed HidHide driver.

The test installation will begin after a full uninstall. Do not implement legacy
profile migration or make the new repository depend on `ConfigurationV1`, legacy
profile registry values, old profile schemas, old shortcuts, or an installed older
application. Preserve stable unified installer identities, protected maintenance
boundaries, and the exact signed driver payload because those are package/driver
safety contracts rather than profile-data dependencies.

## 1. Product contract

The product name is **HidHide Profiles**. Profiles are the main window and primary
workflow. The old Applications / Devices / App Profiles tab model is replaced.

Exactly one saved profile is effective at a time:

1. In **Automatic** mode, the highest-priority enabled application profile whose
   exact executable path is verified running wins.
2. If none matches, the selected Global profile wins.
3. In **Use Global** mode, the selected Global profile wins until the user applies
   a return to Automatic mode.
4. Equal priorities use stable profile ID as a deterministic tie-break. Focus or
   most-recent process must not influence selection.
5. Selecting a profile opens its editor; it never activates or saves it.

Each profile is a complete visibility policy with a default of Visible and explicit
Hidden/Visible rules. “Visible” means not hidden by HidHide. Because the current
driver is machine-global, the UI must not imply per-game driver isolation. Allowed
applications remain a global driver exemption and are not stored per profile.

Edits, profile creation/deletion, mode changes, selected Global changes, pause, and
resume are drafts until the user presses **Apply**. Navigation or close with a dirty
draft offers Apply, Discard, or Cancel. Apply is disabled for a clean editor.

## 2. Architecture and modular driver boundary

Create separable layers with dependencies pointing inward:

- **Profile domain:** stable IDs, revisions, profile kind, executable trigger,
  priority, device identity/rules, selected Global, mode, pause, validation, and
  deterministic winner selection. It contains no registry, UI, process, or IOCTL
  code.
- **Profile repository:** the only durable profile/settings writer. The coordinator,
  process scanner, device notifications, recovery, startup, and shutdown receive a
  read-only repository view and cannot call mutation APIs.
- **Coordinator:** consumes immutable saved snapshots plus process/device events,
  selects the winner, produces desired enforcement state, and publishes status. It
  never serializes or mutates profiles.
- **Enforcement boundary:** define an interface accepting a neutral desired hidden
  device set and global allowed-app state and returning observed/verified state plus
  structured failure. The current adapter uses `FilterDriverProxy` and the existing
  HidHide IOCTL contract. Tests use a deterministic fake. A future driver can be a
  new adapter without changing JSON, selection policy, or UI workflows.
- **Native UI:** edits a detached draft, calls one explicit Apply use case, and
  renders saved, draft, desired, and observed driver state as distinct concepts.

Do not change kernel source, signed INF/SYS/CAT bytes, service/device identity,
IOCTL contracts, Secure Boot, trust stores, or test signing. `HidHide/` remains
archival and excluded from builds.

## 3. JSON repository and Apply transaction

Use the initiating ordinary user's:

`%LOCALAPPDATA%\HidHide Profiles\Profiles\`

Store each profile as `<stable-guid>.json`. Store repository-wide settings in a
separate versioned `settings.json`. Use readable UTF-8, deterministic field order,
strict bounded parsing, and no transient running/connected/effective/dirty fields.

Profile schema v1 includes at least:

- `schemaVersion`, stable `id`, monotonic `revision`, `name`, `kind`
- `enabled`, `priority`, and exact normalized executable path for application profiles
- `defaultVisibility: "visible"`
- device rules with exact persisted identity, last-known friendly name, and
  `visibility: "hidden" | "visible"`

Settings schema v1 includes selected Global ID, Automatic/Use Global mode, pause,
and startup preference. A fresh repository creates a saved `Default` Global profile
whose policy is all Visible. It must not infer a baseline from stale registry data.

Apply performs this boundary in order:

1. Validate the complete draft and expected saved revision/hash.
2. Serialize to a same-directory temporary file using production code.
3. Flush the file, atomically replace the target with write-through semantics, and
   keep a recoverable last-good file where applicable.
4. Reopen, parse, and independently verify ID/revision/content before reporting
   **Saved**.
5. If multiple files change, use an intent/commit record so restart yields the
   complete old or complete new set, never a partial set.
6. Publish the new immutable repository revision to the coordinator.
7. If the applied change affects the effective policy, reconcile through the
   enforcement interface and read it back before reporting **Applied**.

Save and enforcement outcomes are separate. A saved profile remains saved if driver
activation fails. Automatic events must produce zero profile/settings writes.
Malformed or unknown-version files are preserved, skipped from activation, and
reported; never replace them with empty profiles. External edits create a revision
conflict and cannot silently overwrite an open draft.

Remove the new runtime's use of registry App Profiles persistence. Update any CLI
profile commands to use the same JSON repository and Apply semantics, or explicitly
remove unsupported profile mutation commands rather than retain a second writer.

## 4. Main-window implementation

Implement the approved concept at
`artifacts/ux-concepts/hidhide-profiles-concept.svg` as a native, resizable Windows
desktop UI using accessible controls and system colors. Preserve MFC resource and
message-map consistency. Suggested minimum size is 1040 x 680 at 100% scaling.

Always-visible top strip:

- effective profile and verified reason/status
- Automatic / Use Global mode
- selected Global fallback
- staged Pause/Resume action

Left rail:

- search and New profile menu
- Application profiles with running/winner/waiting/disabled/missing status
- multiple Global profiles with selected/fallback/manual status
- Import profiles, Back up all profiles, Open profiles folder
- Settings & recovery and global Allowed apps access

Editor:

- name, dirty state, profile menu, and executable trigger/change action
- connected plus remembered disconnected devices
- read-only **Currently** and editable **After Apply** columns
- explicit accessible Hidden/Visible radio controls
- exact path and device identity in details
- sticky Discard and Apply footer with exact change count and outcome wording

Disconnected exact identities remain editable and survive restart. New devices
default Visible. Never match or transfer a rule solely by friendly name. Composite
or mixed device states must not be flattened deceptively.

Import validates and previews conflicts; imported profiles are disabled until
reviewed and Applied. Export uses the last saved revision unless the user explicitly
chooses Apply first. Back up all exports profiles plus a portable manifest/settings.
Open profiles folder opens the exact ordinary-user repository folder; Open JSON
selects the current profile file.

Use the proposal's explicit error distinctions: save failed/nothing activated,
saved but enforcement failed, observed state unknown, file corrupt, profile conflict,
and unverified executable identity. Never render requested driver state as verified.

## 5. Low-CPU contract

- Keep a native UI; no WebView, animation loop, blur workload, or background charts.
- Replace UI polling/refresh timers with posted change notifications. Repaint only
  changed rows and suspend presentation work while minimized or tray-only.
- Cache the device model; use Windows device notifications with coalesced background
  enumeration rather than periodic full enumeration.
- Take at most one shared process snapshot per scan, prefilter by configured filename,
  then verify exact full paths. Cache identity against process lifetime, not PID alone.
- Stop process scanning when no enabled application profiles exist or Use Global is
  active. Preserve discovery behavior in tray mode when Automatic is active.
- Never read/write JSON, enumerate devices, resolve unrelated process paths, or update
  cosmetic time labels during unchanged steady state.
- Deduplicate desired enforcement state and skip identical driver writes.

Measure the Release client/coordinator process tree against the current native build:
60-second warm-up plus 10-minute steady-state runs for empty, 20 profiles, one match,
competing matches, rapid launch/exit, reconnect, editor, minimized, and tray states.
Report CPU time as percent of one logical core and whole-machine percent, plus private
bytes, OS/hardware, workload, and variance. Proposed gates: idle <= 0.2% of one logical
core, no >10% regression above the documented noise floor, and <= 10 MiB additional
private bytes. Do not claim in-game overhead without a separate 30-minute game trace.

## 6. Mandatory automated tests

Add deterministic tests for:

- winner selection, priorities/ties, app exit fallback, manual Global, pause/resume,
  inaccessible path, PID reuse, multiple instances, and disconnected rules
- strict JSON round trips, Unicode, limits, unknown/corrupt/truncated data, locked or
  read-only target, conflicts, failure injection, and old-or-new atomic recovery
- import/export/back-up collision and traversal handling
- zero repository writes from startup, scans, device changes, pause recovery,
  enforcement failure, minimized operation, and shutdown
- enforcement interface conformance and no-op deduplication
- UI dirty state, navigation/close prompts, clean disabled Apply, keyboard/accessibility
  names, save-versus-activation messages, and supported DPI/high-contrast layouts

Create a dedicated production-path test named and reported distinctly as
**Profile Apply Survives Full Restart**:

1. Launch the production UI and production repository in an isolated test-user data
   directory with deterministic process/device/enforcement adapters.
2. Through real editor commands, create an `F1_25.exe` application profile and mark
   two exact devices Hidden, including one disconnected remembered device.
3. Assert no profile file/revision exists or changes before Apply.
4. invoke the real Apply command and wait on a semantic completion signal, not sleeps.
5. Independently open and validate the committed JSON rather than trusting the app's
   serializer or in-memory model.
6. Terminate both the UI and resident coordinator processes.
7. Start fresh processes against the same directory and assert the same ID, revision,
   executable path, and both device rules are loaded into the editor and coordinator.
8. Exercise startup reconciliation/scan/shutdown and assert the JSON hashes and
   revisions remain unchanged.

This test must use production codec/repository/startup loading and fail if Apply only
updates memory, disconnected rules are omitted, the coordinator survives restart, or
automatic reconciliation rewrites storage. Test seams belong only at filesystem root,
process/device discovery, clock, and enforcement boundaries.

Also run the repository's full documented Release x64 unified CI and packaging checks.
A green fake-driver test is not installed-driver or physical-input proof.

## 7. Completion and evidence

Implementation is ready for handoff only when:

- the fresh Sol adversarial reviewer has no actionable findings after fixes/re-review
- the dedicated full-restart Apply test passes and appears distinctly in output
- full Release x64 unified CI and setup packaging pass
- source diff checks are clean and unrelated worktree files remain untouched
- an unsigned test setup and SHA-256 are provided
- exact test counts, performance evidence actually measured, and validation boundaries
  are reported honestly

Do not install, uninstall, reboot, publish, merge, or modify the user's live driver.
The user will fully uninstall the existing product and perform the final installation
and physical F1/device acceptance test.
