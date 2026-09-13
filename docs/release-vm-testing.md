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

Source snapshot `26a8d506de45b802365c345fd0d44b4a6570d91a` subsequently passed
bare `build.ps1 Ci --configuration Release --platform x64` from a clean detached
checkout, including empty-cache driver acquisition and the diagnostic checks.
Logs: `artifacts/unified-evidence/clean-checkout-ci.log` and
`artifacts/unified-evidence/clean-checkout-release.log`. Full unsigned packaging
with all three recovery sources passed 33 MSI structure checks. Candidate:
`artifacts/clean-validation-26a8d50/artifacts/release-unsigned/HidHide_2.1.0_x64.exe`,
SHA-256 `1D267B9F07AF31E97F184CBF54D10BAA1FFB483128B0240BD13C4DA5EB02EC6B`.
Its manifest reports that exact source commit and `dirty: false`. This is build
evidence for that snapshot, not native acceptance or permission to replace the
guest's pending earlier artifact. Later source changes require fresh packaging.

An exploratory upgrade fixture at source `080d1a67b8bd487f1113310ece873ca58a4d7446`
changes only ProductVersion.props from that baseline to 2.2.0.0. Its clean Ci and
full unsigned packaging passed. Artifact:
`artifacts/upgrade-validation-2.2/artifacts/FIXTURE-ONLY-2.2.0/HidHide_2.2.0_x64.exe`,
SHA-256 `837DA0BED06DF8F8FEBDE0083BD0D9DB5E2BED4B7AE60DB85A083D88B617607E`.
Adjacent FIXTURE-ONLY evidence identifies its test purpose. It has not been
installed and must not be distributed as a public release.

Subsequent packaging hardening checks both application PE images against the
requested architecture before MSI generation. Installer tests passed 63 checks
including the real built x64 executables. A staged application with an altered
ARM64 Machine field was rejected before creating the MSI output directory
(`artifacts/architecture-preflight-20260908/rejection.log`). Package inspection
now passes 45 checks, including x64 summary information and 64-bit components.
These checks do not establish native lifecycle success.

The architecture fix was committed as `51377b5bb9c0ef272f44d3757bc4631ca62b90fe`.
Its clean detached checkout passed Ci (including 61 installer contract checks)
and full unsigned packaging (45 MSI checks). Current candidate:
`artifacts/clean-validation-51377b5/artifacts/release-unsigned/HidHide_2.1.0_x64.exe`,
SHA-256 `438D386E11B6B4DE7C4129A56804A1AB0CFF447942A374A018D05393B479A9FF`.
Manifest and `artifacts/unified-evidence/candidate-51377b5.json` identify its
clean source, all recovery sources and incomplete native acceptance.

At 18:15 EDT a fresh collector failed because the VM was off. The fixed-path
`inspect-install-parent.log` had not been updated by an unsuccessful helper;
its old process list must not be treated as current. Always inspect helper exit
status and snapshot timestamp. After confirming the exact VM identity and Off
state, the VM was started, without launching setup or restoring a checkpoint.
The fresh `after-vm-boot-20260908-181655-433` snapshot recorded boot at 18:16:31
and no HidHide processes, products, registrations, driver resources or recovery
marker. The earlier Burn log records a system shutdown request at 18:03:11;
the older exit-code file predates that attempt and is not its terminal result.

Candidate 51377b5 was then copied with hash verification into the new guest
directory `C:\ReleaseTests\candidate-51377b5` and launched once at 18:19:20
under VMAdmin's filtered interactive token. At 18:19:46, fresh evidence
`artifacts/release-vm/state-51377b5-20260908-181945.json` showed bundle PID 4192,
bootstrapper PID 8268 and consent PID 4828 live in session 1, awaiting guest UAC.
There was no terminal result. Use `check-51377b5.ps1` and its new timestamped
output to observe this attempt; do not infer status from older fixed-path logs
or launch another installer while this handle remains live.

The original candidate attempt and retry1/retry2 subsequently terminated with
Win32Exception 1223 (Windows elevation cancelled), before installation. Fresh
inventories remained empty. Their logs are preserved in timestamped
`candidate-failure-*` evidence folders. A test launcher guard was corrected to
recognize VMAdmin's enhanced session by its explorer process owner SID; the
console-only username check had stopped one launch before creating an attempt.

Retry3 of the exact 51377b5 artifact started at 21:25:14 EDT on 2026-09-08,
under an ordinary VMAdmin token. After both elevation steps, installation
returned 3010 at 21:26:22. Preboot inventory at 21:27:15 recorded the private
2.1.0 MSI, one visible Burn product, a healthy root device using oem1.inf and
driver version 1.4.181, an available control device, and matching setup
WaitingForReboot / driver RebootRequired journals. Reboot continuation and the
remaining lifecycle matrix are not yet established by this first-stage result.

Source-side UX hardening now translates only controller launch error 1223 into
cancellation exit 1602 with an explicit elevation-not-approved message. It keeps
existing recovery data and does not reclassify later native failures. The
bootstrapper suite passed 65 checks. This change is not in the installed test
artifact; maintenance must keep using that exact original artifact.

The VM restarted normally at 21:28:25. The same retry3 EXE resumed once under
ordinary VMAdmin at 21:30:45 and returned 0 at 21:30:50. Full post-completion
evidence `retry3-complete-20260908-213150` recorded setup Phase Complete,
driver Status Committed/Reboot false, no maintenance marker, one visible Burn
registration, the private MSI, one healthy root device and an available control
device. Secure Boot remained enabled and the installed SYS matched the pin with
a valid signature. This establishes the clean offline install/reboot/resume
cycle for that specific artifact; GUI/profile semantics and the broader matrix
remain separate gates.

`retry3-license-preboot.json` also proves an environment issue: expired Windows
Enterprise evaluation (LicenseStatus 5, zero grace) and shutdown events explicitly
initiated by wlms.exe because its license expired. No activation, time or trust
changes were made. A separate test VM is being prepared from the user-supplied
Windows 11 25H2 retail ISO for sustained testing.

Same-artifact repair returned 0 at 21:33:32, without reboot. Evidence
`retry3-repair-complete-20260908-213406` verified Complete/Committed journals,
no marker, unchanged application/runtime/driver hashes and unchanged filters.
Ordinary-user CLI help, version, cloak-state and inv-state all returned 0
(`gui-smoke2-20260908-213735`); version was 2.1.0.0 and both states were off.
The GUI launched as PID 6048 in the ordinary user's session and displayed its
Applications tab with fork version and baseline-applied status. Tab navigation,
About and normal tray exit were not verified: Computer Use reported the VM
connection's integrity level was higher than the helper, and input did not work.

An ordinary uninstall with that GUI running closed it through maintenance
handoff, then exited 1 on declined Windows elevation before native removal.
`retry3-uninstall-failure-20260908-214222` confirmed the installed product and
driver remained healthy, with no marker or new journal. This is not an uninstall
pass. Preserve this state and use the same setup for the next attempt.

The separate `HidHide-Retail-Pro-25H2` VM was created from the supplied ISO
(SHA-256 `768984706B909479417B2368438909440F2967FF05C6A9195ED2667254E465E3`),
verified image index 6 / Professional / AMD64 / build 26200. It has Secure Boot,
vTPM and disconnected networking. Windows Setup is waiting at its normal
product-key page, with "I don't have a product key" available; no activation or
licensing workaround was used. Finalized guest baseline is still pending.
