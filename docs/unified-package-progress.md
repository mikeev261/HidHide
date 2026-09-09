# Unified package progress

Current scope: practical, generally working unsigned release tested on the user's local Windows 11 x64 PC. The user explicitly cancelled VM testing and deferred the exhaustive legacy, interrupted-install and power-loss matrix. No automatic publishing or merge. Driver signatures, Secure Boot, IOCTL contracts and user settings remain unchanged.

## Current checkpoint — 2026-09-08 evening

The old local 2.0 preview completed normal uninstall/reboot/resume, exit 0. Strict empty-state verification found no product, node, service, driver package, SYS or HidHide filter references. Evidence: artifacts/local-validation-20260908/host-empty-after-resume.json.

The new 2.1.0.0 candidate from clean source59c03f2 installed and returned3010. Current maintenance marker58e29028-e172-4b06-97ba-2a4ab30e39ae is WaitingForReboot. One visible Burn entry, private MSI and healthy exact Microsoft-signed driver were verified with Secure Boot enabled. Installation completion after the next reboot is still pending.

Authoritative handoff: artifacts/local-validation-20260908/RESUME-AFTER-REBOOT.md. It records the exact candidate path/hash, current marker, actual-user SID, settings backups and startup preference restoration. Use the same candidate for continuation; preserve all journals.

An old test-build HidHide startup entry was backed up and temporarily disabled during recovery. Restore the user's enabled preference to the verified installed app after setup completion. Codex autostart is verified.

## Verified and remaining

Clean unified Ci and full unsigned packaging passed for this candidate, including native/managed suites and45MSI checks. Owned code is unsigned; the pinned upstream driver remains Microsoft-signed. The earlier VM candidate passed clean offline install/reboot/resume and repair; this is separate evidence, not a substitute for the current local candidate.

Next: reboot and complete the 2.1 transaction, then test same-artifact repair, basic GUI/profile activation and baseline restoration, normal exit, and practical uninstall/reinstall as needed. Real-controller functionality requires observation on this PC. Do not call the build generally validated until these practical checks are complete.

The broader release matrix remains documented in docs/release-readiness.md as deferred coverage. Historical host/VM results are in docs/native-lifecycle-testing.md and docs/release-vm-testing.md; implementation history is in docs/unified-package-history.md. No further VM work is requested.
