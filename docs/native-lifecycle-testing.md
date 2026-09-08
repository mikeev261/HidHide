# Native lifecycle testing — in progress, not passed

## Current checkpoint — 2026-09-08 16:34 EDT

Fixed package resume returned 0 after confirmed boot at 14:50:39 and cleared the
maintenance marker. Installed GUI passed all three tabs with existing F1 profile
preserved. Same-artifact repair with GUI under CDB returned 0; no AV, invalid-handle
or heap-corruption trap fired. CLI cloak/inverse remain off. Driver healthy with
pinned SYS. One visible bundle `{560D3526-301D-4974-81DD-5AC47E4AAD1A}` and one
hidden private MSI were verified. Existing C++ regression executable passed 90
tests. Evidence: gui-fixed-postboot-resume.log, installed-fixed-gui-debug.log,
installed-fixed-repair.log, final-cpp-tests.log.

Normal tray Exit passed: user selected "Exit and restore device settings" while
CDB monitored the installed GUI. The process exited and no AV, invalid-handle or
heap-corruption trap fired (installed-fixed-tray-exit.log). Baseline queries still
report cloak/inverse off, with no maintenance marker. Current
installation is healthy; no reboot pending from HidHide maintenance.

The common install/resume, GUI, repair and prior ordinary uninstall/resume paths
have real-host evidence. This does not close related-bundle upgrades, clean/offline,
fault-injection, cancellation or release-signing gates.

## Current checkpoint — 2026-09-08 14:49 EDT

Boot at 14:28:50 confirmed. **Ordinary uninstall/resume passed**, exit 0:
the installer removed the pinned leftover SYS using its journaled removal step.
Read-only inspection confirmed no service, control device, node, driver package,
binary, owned filters, maintenance marker, or HidHide uninstall registration.
No manual cleanup was used for this cycle. Evidence: normal-uninstall-postboot.log.

Clean-installed `artifacts/lifecycle-gui-fixed/HidHide.Setup.exe`; MSI actions
succeeded and setup returned 3010. The driver is healthy and the installed GUI
hash matches the frozen package (3E886E5D410C66462A7D16C7978AE2B778E70D9FAB437C8100D82FF4A49B9D40).
29 package structure checks passed. Marker: `012e14e8-0b73-4294-988b-f6fbe8223162`.
Evidence: gui-fixed-clean-install.log. Next: reboot, resume **lifecycle-gui-fixed**
setup, then test installed fixed GUI (tabs, normal tray Exit, maintenance handoff)
and same-artifact repair. Full acceptance matrix is still incomplete.

## Current checkpoint — 2026-09-08 14:23 EDT

Boot at 13:16:11 confirmed; installation resume returned 0 and cleared the marker.
Installed GUI passed Applications, Devices, and App Profiles tab navigation;
existing F1 profile remained. CLI reported cloak/inverse off. An initial duplicate
debugger instance held the coordinator lock; both test instances were terminated,
then a single desktop instance was used. Do not launch GUI from the sandbox.

Repair returned 0, but debugger captured shutdown access violation/invalid handle.
Static review found a double-destruction bug: global lease destroyed before global
app destructor reset it. Lease is now InitInstance-local, outliving the dialog.
Rebuilt GUI completed repair handoff under CDB with AV, invalid-handle and heap
corruption traps without those faults. New package: `artifacts/lifecycle-gui-fixed`.
Normal tray Exit and installed-new-package GUI verification remain to be tested.

Ordinary uninstall of `artifacts/bundle-recovery/HidHide.Setup.exe` completed MSI
actions and returned **3010**. Node/package and owned filters are gone; service and
binary await reboot. Marker: `63cb5a95-31fa-4dfd-8300-e7ff52ec973c`.
After restart, resume using **the same bundle-recovery setup**, verify strict empty
driver state and marker clearance, then clean-install lifecycle-gui-fixed and test
its GUI, repair, uninstall/reinstall. Preserve journals; no manual state cleanup.

Evidence: corrected-postboot-resume.log, corrected-repair.log,
gui-single-instance-debug.log, gui-handoff-capture.log, gui-fixed-debug.log,
fixed-gui-handoff-repair.log, corrected-normal-uninstall.log. User authorized PC
restarts after configuring Codex auto-start; startup shortcut is configured.

Older checkpoints below are historical.

## Current checkpoint — 2026-09-08 12:51 EDT

User-approved file cleanup succeeded in a separate journal, preserving the old
incomplete transaction. Windows Installer then removed the application MSI.
After explicit approval, the inspected recovery bundle completed with exit 0:
Burn removed its original registration and cache through its own uninstall path.
Read-only verification confirmed no driver resources, maintenance marker, or
HidHide registration remained. This recovery is not a normal uninstall pass.

Fresh installation of `artifacts/bundle-recovery/HidHide.Setup.exe` completed MSI
actions with result 0 and returned **3010 (restart required)**. No automatic
restart occurred. Driver inspection is healthy: ROOT\\HIDHIDE\\0000, oem34.inf,
exact pinned SYS hash, and all three filters. The maintenance marker is
`ff307a0a-b6be-4151-9393-128b996ba5e9`; installed bundle identity is
`{A49DD11E-DB93-4244-A630-D54456894C64}`. Do not clear the marker.

After the user restarts, run the same setup artifact to resume this transaction,
then investigate the unresolved GUI heap corruption and test repair, ordinary
uninstall, and reinstall. Regression checks: 113 driver, 77 controller, 31 installer;
29 MSI structure checks passed. Evidence: `burn-registration-cleanup.log` and
`corrected-clean-install.log` in `artifacts/unified-evidence`.

The older checkpoints below are historical and superseded by this state.

## 2026-09-08 recovery checkpoint

Real boot confirmed at 12:08:08 EDT. Service, control device, root node, package and
all owned filters are gone. Exact pinned SYS remains with TrustedInstaller owner;
an administrator File.Delete attempt failed access denied. Original journal is
now RecoveryRequired with an incomplete RemoveBinary intent; it must be preserved.
Added scoped restore-privilege, handle-hash-verified file deletion without ACL or
ownership changes, plus tests for post-reboot leftover-file cleanup (113 checks).
That privileged cleanup has NOT been executed or proven successful.

Automatic approval review rejected a proposed retry that removed an incomplete
journal step and rewrote its boot identity. Do not run that script or bypass the
rejection. Prepared `artifacts/unified-evidence/approved-file-cleanup.ps1` instead:
it requires explicit user approval, records a NEW cleanup transaction, and retains
the original journal, boot identity, marker, MSI and bundle registrations unchanged.
No uninstall registry entry has been deleted. Cleanup of remaining registrations
must use supported installer execution, not registry deletion to mimic success.

GUI WER reports were read; they confirm heap corruption and contain no retained
dump. The crash remains unresolved. No new confidence claim is warranted.

## Latest checkpoint after first coordinated restart

- Original install driver journal successfully resumed against a different real
  boot and exact native identities. Failed MSI had fully rolled back applications.
  A separately journaled corrected install transaction preserved the old evidence.
- Corrected bundle completed **actual install with exit 0**. Controller verified
  payloads and cleared maintenance marker. Actual same-artifact **repair exited 0**.
  Registry inspection found one visible bundle and one hidden private MSI.
  Installed CLI cloak/inverse queries both reported off.
- Actual uninstall failed after successful node/package removal because pending
  service deletion threw from Inspect. Source now represents pending deletion as
  explicit state and returns reboot-required from owned removal instead of throwing.
  Tests pass (110 driver, 77 controller); corrected build is
  `artifacts/lifecycle-removal-fixed` and is NOT lifecycle-validated.
- **GUI startup crashed with heap corruption (0xc0000374)** before uninstall.
  This is NOT resolved. CDB launch while the marker is active exits through the
  admission guard before reaching the original crash; do not count it as a pass.
  Need reproduce with symbols/dump after native recovery, inspect the runtime set
  (compiler 14.52, packaged MFC 14.51), and fix the actual stack before acceptance.
- Current marker: `00fb8a97-4101-4d6a-8da4-54ee6a215c87`.
  Node/package gone, filters detached, service pending deletion and driver still
  loaded. MSI rollback retained applications/registration. Another user-coordinated
  restart is required to finish native removal, followed by explicit recovery of
  old MSI registration and corrected reinstall. Do not blindly run old uninstall.
- Evidence: `native-install-after-boot.log`, `native-repair.log`,
  `native-uninstall.log`, `native-uninstall_000_Unified.log`, `gui-crash.log` in
  `artifacts/unified-evidence`. No confidence claim until remaining cycles and GUI
  are actually tested.

The user explicitly requested actual host lifecycle testing after repeated preview
failures. Do not offer another preview as working on the strength of build,
simulation, or preparation-only checks.

## 2026-09-07 host results

1. User's v6 install: Burn planning passed; Apply rejected a null HWND before MSI
   execution. Fixed with an independently pumped STA parent window.
2. Recovered transaction `9e424b03-a558-4a09-9f8a-bd78054a8da3` only after verifying
   protected Prepared/zero-step driver journal, unchanged native state, no product
   registration or installed application, and Burn's explicit pre-Apply rejection.
   Saved the original setup journal; retained maintenance exclusion.
3. Native install reached the actual deferred action. Driver staging, binding and
   class-filter attachment completed, yielding RebootRequired. MSI then failed
   because the action called Session.SetMode from deferred context. Application
   files rolled back; driver rollback correctly refused pending-reboot replay.
4. Removed the invalid deferred SetMode call. The controller already reads the
   protected reboot result and reports restart required after successful MSI.
5. Froze BA/controller files within build output to prevent later test builds from
   silently replacing a controller containing embedded recovery resources.

Evidence: `artifacts/unified-evidence/lifecycle-install-2.log` and
`lifecycle-install-2_000_Unified.log`; corrected build is in
`artifacts/lifecycle-fixed`. Structure and read-only launch pass. These do not
prove the corrected MSI installation or complete any lifecycle acceptance gate.

## Restart checkpoint

Driver is healthy, root node `ROOT\HIDHIDE\0000`, package `oem34.inf`, exact pinned
SYS, all three HidHide filters present. MSI/application installation rolled back.
The protected transaction and HKLM marker remain. Do not clear the marker or replay
the old failed MSI. No automatic restart has occurred.

After a user-coordinated restart, inspect the original protected journals and native
state first; complete explicit recovery using the corrected package. Then test real
install completion, same-artifact repair, uninstall, reinstall, restart/resume,
one visible Installed Apps registration, payload hashes, and GUI/CLI behavior.
Preserve baseline/profile settings and evidence. Do not report confidence until
these pass; other clean/offline/upgrade/fault-injection gates remain separate.
