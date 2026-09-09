# Unified package progress

Current scope: practical, generally working unsigned release tested on the user's local Windows 11 x64 PC. The user explicitly cancelled VM testing and deferred the exhaustive legacy, interrupted-install and power-loss matrix. No automatic publishing or merge. Driver signatures, Secure Boot, IOCTL contracts and user settings remain unchanged.

## Current checkpoint — 2026-09-08 evening

The old local 2.0 preview completed normal uninstall/reboot/resume, exit 0. Strict empty-state verification found no product, node, service, driver package, SYS or HidHide filter references. Evidence: artifacts/local-validation-20260908/host-empty-after-resume.json.

The 2.1.0.0 candidate from clean source 59c03f2 completed install/reboot/resume with exit 0. Transaction 58e29028-e172-4b06-97ba-2a4ab30e39ae is Complete/Committed, with no maintenance marker. One visible Burn entry, private MSI and healthy exact Microsoft-signed driver were verified with Secure Boot enabled.

Authoritative handoff: artifacts/local-validation-20260908/RESUME-AFTER-REBOOT.md. It records the exact candidate path/hash, current marker, actual-user SID, settings backups and startup preference restoration. Use the same candidate for continuation; preserve all journals.

The user's enabled startup preference has been restored to the verified installed app at Program Files/HidHide, replacing the backed-up obsolete test-build command. Codex autostart remains configured. The user's registry settings export immediately after installation matched the pre-test export.

Same-artifact repair completed with exit 0 and no restart, including shutdown and background restart of the running coordinator. Transaction 6bd40111-a0c6-47b5-817f-41ccb9749802 is Complete/Committed. A temporary process profile applied exactly one joystick interface in the real driver's effective state and restored the exact baseline after process exit; original profiles were preserved. The user exercised tray Exit, the process ended, baseline was verified, and no HidHide crash event was found. Evidence is under artifacts/local-validation-20260908.

Reopening the background app exposed a real usability defect: the existing window opened, but the second process also displayed an ownership warning. Commit 85bce35 fixes acknowledged activation within the same user/session and preserves the warning when no local owner responds. Version 2.1.1.0 passed clean unified Ci (95 native tests, managed suites) and 45 MSI checks. Its normal local upgrade and repair both returned 0 without reboot. Installed reopening now opens the existing window and the second process exits 0 with no popup. About correctly distinguishes fork 2.1.1.0 from driver 1.4.181.0.

The repaired final build also passed a real effective-state profile activation/restoration check with the original profiles preserved. One repair attempt was refused before mutation while About was open; closing the dialog and retrying succeeded. Close child dialogs before maintenance. Both attempt logs are retained.

Final setup: artifacts/local-release-validation-85bce35/artifacts/local-test-candidate/HidHide_2.1.1_x64.exe. SHA256: 3D2FEB6F1C6B31FEC54FC013819820BFBA77880E04984C1A5FE8F5E276B481AC. Clean source: 85bce353ce5c9100e9b154aeed30c1610a3360fe. All three recovery media are included. This exact artifact is now installed; use it for maintenance.

## Verified and remaining

Clean unified Ci and full unsigned packaging passed for this candidate, including native/managed suites and45MSI checks. Owned code is unsigned; the pinned upstream driver remains Microsoft-signed. The earlier VM candidate passed clean offline install/reboot/resume and repair; this is separate evidence, not a substitute for the current local candidate.

Practical local installation, repair, reopening and automatic profile state changes have been validated. Controller enumeration and effective profile settings were exercised locally; actual game input remains a user check. Standalone removal and normal tray exit of the final candidate were not repeated (normal removal passed on 2.0 and normal tray exit on 2.1). The broader failure matrix remains untested. No further restart or setup action is currently required.

The broader release matrix remains documented in docs/release-readiness.md as deferred coverage. Historical host/VM results are in docs/native-lifecycle-testing.md and docs/release-vm-testing.md; implementation history is in docs/unified-package-history.md. No further VM work is requested.
