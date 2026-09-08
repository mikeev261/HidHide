# Release-readiness audit

Scope is the complete original unified-package request and docs/unified-package-design.md. Unsigned public setup is explicitly accepted. Open requirements are not waived by this checklist.

| Requirement | Evidence / remaining gate |
|---|---|
| One Burn EXE, private MSI, visible fork entry, UI shortcut | 2.0 host evidence; repeat final candidate |
| Stable identities, coherent version, x64 target | ProductContract/props and deterministic ProductCode checks; native compatible upgrade matrix open |
| Exact signed INF/SYS/CAT/license; no kernel/IOCTL change | Manifest, tamper tests, native hash; final source/package audit needed |
| Empty-cache acquisition and unified clean-checkout CI | Clean detached snapshot 51377b5 passed bare Ci and empty-cache acquisition; repeat for any later final production changes |
| Optional owned-code/MSI/Burn signing and unsigned mode | Optional signing implemented, certificate path untested; deliver explicitly unsigned |
| Version/source commit/source changes/SHA256/manifest/logs | Clean 51377b5 unsigned artifact and all recovery sources recorded in release-vm-testing.md; final native acceptance still pending |
| Clean install, reboot continuation, repair, uninstall/reinstall | 2.0 real host logs; final candidate verification pending |
| Upstream-only, companion-only, both, companion99 migration | Policy tests exist; native matrix open |
| Prior unified upgrade, rollback, downgrade, repeated operations | Protocol implemented and 2.2 version-only fixture built; native two-version matrix open |
| Missing/damaged driver repair, unknown newer driver preservation | 188 driver and 144 controller checks passed; isolated validation open |
| Cancellation before/after legacy removal, interrupted recovery, power loss | Recovery UX implemented with legacy file/service/filter proof; isolated matrix open |
| Standard user with alternate administrator credentials | Initiating-SID/administrator pipe ACL fix and 3 security checks passed; integration test open |
| Other-user coordinator block and initiating-user startup | Existing protocol checks; cross-user integration open |
| Profiles, baseline overlap, persistent disable, external-edit conflict | 91 C++ tests and earlier probes; final installed semantics validation open |
| GUI/CLI normal use and exit/maintenance handoff | Fixed 2.0 package passed CDB; final candidate recheck needed |
| Fork/actual-driver version, missing/reboot diagnostics, credits | About visually verified; modal-maintenance rejection and normal exit regression passed |
| Secure Boot load and physical controllers after reboot | Secure Boot true/driver healthy; physical functional confirmation needed |
| Unrelated filters preserved, no dangling HidHide references | Unit checks and 2.0 strict-empty uninstall; final snapshot comparison needed |
| Clean offline installation/runtime and no setup downloads | Embedded payload inspection; isolated offline test open |
| Final docs, source snapshot, supported paths, rollback instructions | Pending final candidate and acceptance matrix; no auto-publish |

Environment: artifacts/unified-evidence/release-test-host-inventory.json recorded Secure Boot enabled and the original Ubuntu VM. A new isolated Windows11 evaluation VM now exists under artifacts/release-vm with SecureBoot/vTPM and disconnected network. Destructive input-driver tests require isolated Windows, not deliberate damage to this primary host.
