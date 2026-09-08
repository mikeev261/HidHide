# Isolated release validation

Current status: VM provisioned; native installation has not completed. These are
exploratory candidates, not the final release acceptance result.

`HidHide-Release-Win11-25H2` runs Windows 11 Enterprise Evaluation build 26200,
with Secure Boot and virtual TPM enabled and its network disconnected. Clean
Windows and clean desktop checkpoints are preserved. The installation DVD is
ejected. Provisioning and per-case evidence live in `artifacts/release-vm`.
Private generated credentials and unattended media must never be distributed.

The first candidate, `release-2.1.0-candidate/HidHide_2.1.0_x64.exe`, was copied
and hash-verified in the guest. It launched from an ordinary filtered VMAdmin
token. Attempt 1 exited before MSI execution when elevation was cancelled.
Attempt 2 was still awaiting guest UAC approval at 17:27 EDT on 2026-09-08.
Read-only inventory confirmed no product registration, driver, or maintenance
marker had been created. This proves neither installation nor rollback.

The read-only `collect-inventory.ps1 -Case <name>` helper saves timestamped
inventories and logs. It records MSI identities through Windows Installer APIs,
Burn registrations, driver resources and file hashes, signature status, filter
ordering, Secure Boot, a control-open probe, runtimes, shortcuts and selected
journal metadata. It excludes configuration values and does not send IOCTLs.

Full integrated CI subsequently passed with 91 native tests, 36 installer
contract checks, 153 driver checks, 133 controller checks, 3 pipe security
checks, 53 bootstrapper checks, 7 source-evidence checks and 33 MSI structure
checks (`artifacts/unified-evidence/release-integrated-ci.log`).

The unsigned integration-2 candidate includes all three legacy recovery sources:
`artifacts/release-2.1.0-integration-2/HidHide_2.1.0_x64.exe`, SHA-256
`7CF602D651E88F9E67F34B6C900FD25B642AEE17D05462623B5EAED3874EFC43`.
It predates a subsequent cancellation-resume baseline fix and is not final.
Retain each artifact and its manifest; never substitute a rebuilt same-version
bundle for an installed bundle during maintenance.

The complete outstanding native matrix remains in [release readiness](release-readiness.md).
Final acceptance must restart from a clean checkpoint using a frozen final
candidate, while preserving earlier failure evidence.

At 17:56 EDT, a second read-only snapshot still showed the same guest UAC prompt
and no installed HidHide resources (`evidence/awaiting-consent-20260908-175651-407`).
Do not start a duplicate installer while that attempt is pending.

Recovery integration subsequently passed full Ci at 17:53 EDT with 188 driver,
144 controller, 10 legacy-file, 27 service, 30 legacy-engine, 11 filter, 3 pipe,
55 bootstrapper, 36 installer, 7 provenance and 91 native checks. The final
diagnostic refinement added 20 codec checks, with the controller and BA suites
passing again. The current provisional candidate is
`artifacts/release-2.1.0-integration-4/HidHide_2.1.0_x64.exe`, SHA-256
`3A6940336C3A3173F906F8734ADA0B543D1696F7253D66C81409959998B86AB9`.
Its packaging and 33 MSI checks passed. It remains an unsigned dirty-worktree
test build; source commit/clean-checkout and native acceptance gates are open.
