# Unified package progress

Current phase: 7–8, release implementation and validation. Goal: release-ready unified Windows 11 x64 setup. Not achieved yet.
User explicitly accepts an unsigned public setup; driver signatures remain unchanged. No automatic publishing or trust changes.

Latest user scope (2026-09-08 evening): practical, generally working personal-use release; exhaustive edge-case validation deferred. User cancelled VM testing and requested local-PC testing. Current local checkpoint is artifacts/local-validation-20260908/RESUME-AFTER-REBOOT.md. Normal removal of the local 2.0 preview returned3010; reboot and same-setup continuation are required before installing the new clean59c03f2 candidate. Preserve settings and all journals. The VM plans below are historical pending work, not the current next action.

## Current state and evidence

Host has 2.0.0 development package artifacts/lifecycle-gui-fixed installed, healthy pinned driver, no maintenance marker. Normal tray exit completed. See [native evidence](native-lifecycle-testing.md) before host mutations.
Verified: fresh install/reboot/resume, GUI tabs and F1 profile preservation, CLI baseline, same-artifact repair with GUI running, normal exit and maintenance handoff under CDB, ordinary uninstall/reboot/resume with strict empty-state verification, reinstall. Secure Boot is enabled. These are host results, not the full acceptance matrix.

## Work in progress

- Unified default CI/release, automatic verified acquisition, optional signing and artifact provenance implemented; bare Ci and public unsigned packaging passed.
- Compatible related-bundle upgrade protocol; 2.0 previews lack this protocol. Candidate version 2.1.0.0.
- Alternate-administrator credential elevation while retaining initiating-user ownership; pipe ACL checks pass, native user test pending.
- GUI fork/driver version diagnostics and credits visually verified. Modal maintenance regression fixed and live checks passed (about-modal-regression.log).
- Latest native rebuild passed 91 tests (release-native-latest.log). Safe pre-Apply cancellation passed 53 BA and 116 controller checks plus 3 security checks; 7 source-evidence checks passed.
- Full recovery integration Ci passed (release-recovery-integrated-ci.log): 91 native, 188 driver, 144 controller, 36 installer, 55 BA, 3 pipe, 10 legacy-file, 27 service, 30 legacy-engine, 11 filter and 7 provenance checks; 33 MSI checks. Explicit legacy restore and rollback across reboot now have production paths and automated coverage.
- Clean source 51377b5 passed unified Ci and full unsigned packaging with all recovery sources and 45 MSI structure checks. That exact artifact is undergoing native testing; see release-vm-testing.md for its path and hash.
- The same candidate passed clean offline install (3010), normal guest reboot, continuation (0), and same-artifact repair (0). Ordinary-user CLI queries and GUI launch passed; GUI interaction and uninstall remain open. New source commit 64ef587 improves declined-elevation diagnostics, with 65 bootstrapper checks passed, but is not in the installed test artifact.

## Open gates

See [release audit](release-readiness.md). Legacy matrix including companion 99; compatible unified upgrade/rollback/downgrade; damaged-driver repair; explicit interrupted recovery; cancellation; clean offline runtime; alternate-account UAC; isolated fault injection; and final source/artifact acceptance remain open.
The isolated Windows 11 Enterprise evaluation VM has Secure Boot/vTPM and no network. Its expired evaluation license causes Windows-initiated shutdowns; a separate VM using the user-supplied retail ISO is being prepared for sustained testing. Do not damage the primary host to simulate disposable-machine failures. Unsigned release is accepted; optional signing is implemented but has not been tested end-to-end with a trusted certificate.

## Next action

Continue uninstall using the exact 51377b5 setup from guest directory C:\ReleaseTests\candidate-51377b5-retry3 after its Windows elevation prompt can be approved. The previous uninstall stopped before native removal. Finish the second VM's normal Windows Setup product-key screen and finalize a clean baseline for sustained testing. Observe fresh process/result evidence, not old fixed-path logs. Complete the remaining native matrix, then freeze and validate the final source/artifact. Explicit legacy restore and rollback across reboot remain unverified in the VM. Preserve the complete goal and all old journals.
Earlier milestones are retained in [history](unified-package-history.md).
