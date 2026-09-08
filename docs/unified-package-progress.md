# Unified package progress

Current phase: 7–8, release implementation and validation. Goal: release-ready unified Windows 11 x64 setup. Not achieved yet.
User explicitly accepts an unsigned public setup; driver signatures remain unchanged. No automatic publishing or trust changes.

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
- Unsigned integration-3 candidate built with all legacy recovery sources; still provisional, pending diagnostic refinements and native validation. See release-vm-testing.md.

## Open gates

See [release audit](release-readiness.md). Legacy matrix including companion 99; compatible unified upgrade/rollback/downgrade; damaged-driver repair; explicit interrupted recovery; cancellation; clean offline runtime; alternate-account UAC; isolated fault injection; final docs and clean-checkout build remain open.
A new isolated Windows 11 Enterprise evaluation VM is now available with Secure Boot/vTPM and no network; its clean desktop checkpoint is available. Do not damage the primary host to simulate disposable-machine failures. Unsigned release is accepted; optional signing is implemented but has not been tested end-to-end with a trusted certificate.

## Next action

Guest UAC approval is pending for the original exploratory candidate. Do not launch another installer until that attempt ends. The latest provisional artifact is release-2.1.0-integration-4; full recovery Ci, subsequent diagnostic tests and packaging passed. Continue the native matrix after approval, then freeze the final source snapshot and validate a clean checkout. Explicit legacy restore and rollback across reboot remain unverified in the VM. Preserve the complete goal and all old journals.
Earlier milestones are retained in [history](unified-package-history.md).
