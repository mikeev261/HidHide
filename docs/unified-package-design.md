# Unified HidHide package design

Status: implementation contract, not a claim of completed lifecycle validation.
Original decision date: 2026-09-07; public-MSI revision: 2026-09-17. Source baseline:
`4efeeabd038b2ff75929eed2df9c7cdf4b95947d` (PR #20 included).

## Product and ownership

The current product decision supersedes both the companion-only restriction and
the custom Burn bootstrapper. One public WiX 5.0.2 per-machine x64 MSI,
**HidHide Profiles**, publisher **HidHide Profiles**, owns the visible Installed
Apps entry, standard Windows Installer UI, applications, unchanged signed driver
payload and lifecycle actions. Neither legacy product is part of the chain; a
recognized legacy installation is blocked for normal removal instead of being
silently migrated. The application directory is `%ProgramFiles%\HidHide`; one
Start-menu shortcut opens `HidHideClient.exe`. This ordinary-user native coordinator
launches the independent `Editor/HidHideProfiles.exe` Electron/React editor. The
editor fully exits when closed; native profile monitoring continues. The existing
repository, enforcement and recovery services remain authoritative. An authenticated
same-user JSON bridge uses version/hash checks for mutations. Maintenance rejects
handoff while an editor process is open. `HidHideCLI.exe` keeps its name.

New randomly generated stable upgrade identities (do not regenerate):

| Family | Upgrade identity |
|---|---|
| Retired unified Burn bundle (historical detection only) | `C62D8280-B0B1-42FD-8969-084CC64F9D5B` |
| Unified public MSI | `A7F7B763-29B4-47FB-9B00-DB18AFA5EB32` |
| Legacy upstream MSI | `8822CC70-E2A5-4CB7-8F14-E27101150A1D` |
| Legacy companion MSI | `7078E839-3A07-4FA9-BC3A-7677356C88CF` |

New ProductCodes must change on major upgrade; same-version repair uses the same
package. Compare the three MSI version fields, reject unified downgrades. Legacy
99.0.0.0 is a separate-family migration, never a reason to allow downgrades.

## Artifact evidence

The [selected release](https://github.com/nefarius/HidHide/releases/tag/v1.5.230.0)
contains `HidHide_1.5.230_x64.exe`, an Advanced Installer EXE, not a Burn bundle.
Downloaded SHA-256:
`F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6`.
Its Authenticode verification returned Valid with Nefarius Software Solutions e.U.
Extraction used only the documented
[`/extract <existing absolute directory>`](https://www.advancedinstaller.com/user-guide/exe-setup-file.html)
interface, never installation or administrative MSI execution.

Extracted MSI SHA-256:
`974F2F56BB9D87E760FFDC757BBAA1AD94CEFB57431D73B1E5E209F262B8C9F7`.
Read-only Windows Installer database queries establish ProductCode
`01E0AB21-D1CC-42B4-9DFF-84FFE4F26DAF`, ProductVersion `1.5.230`, and the upstream
UpgradeCode above. Full table evidence is in
`artifacts/unified-evidence/upstream-msi-tables.json`. WiX decompilation encounters
an XML-invalid character in an upstream PowerShell action; its partial output
is not authoritative. Direct table queries succeed.

The actual INF declares `DriverVer=10/31/2023,1.4.181.0`, `NTamd64`, a System-class
`root\HidHide` device and service `HidHide`. Its only SourceDisksFiles/CopyFiles
payload is `HidHide.sys`. The package also supplies `hidhide.cat` and `LICENSE.rtf`.
The original catalog passes `signtool verify /kp`; explicit `/kp /c` membership
verification passes for both INF and SYS. The catalog signer is Microsoft Windows
Hardware Compatibility Publisher. Do not edit any signed file or run Inf2Cat.
The INF preserves initial blacklist/whitelist values but resets Active to zero on
installation: configuration must be explicitly restored after driver replacement.

The MSI executes root-node creation at 6401, driver install at 6402, and three
upper-filter additions at 6403–6405. Uninstall removes filters at 1601–1603,
root nodes at 1604 and driver at 1605. Class GUIDs are:

- HID: `745A17A0-74D3-11D0-B6FE-00A0C90F57DA`
- XNA composite: `D61CA365-5AF4-4486-998B-9DB4734C6CA3`
- Xbox composite: `05F5CFE2-4733-4950-A6BB-07AAD01A3A84`

These required actions have type 1106 (including continue-on-error), which the
fork must not reproduce. The bundled nefconw.exe reports 1.2.0.0, SHA-256
`1482FA240CAD984E02206427F1EB211E62C9A44B058484FC3E83CCB5B1A1FBCA`.
The stale install.cmd/uninstall.cmd reference absent files: exclude them.
The upstream Watchdog, updater and UI are excluded from the final product.

Legacy companion metadata was read from the actual local packages:

| Version | ProductCode | Evidence |
|---|---|---|
| 1.0.0.0 | `B7E9D4A2-6F31-4E88-9C0D-1A2B4C4D5E70` | artifacts/test-installer-8a32edf-x64/msi/HidHideAppProfiles.msi |
| 99.0.0.0 | `B7E9D4A2-6F31-4E88-9C0D-1A2B6C4D5E70` | artifacts/msi/x64-companion-99-independent/HidHideAppProfiles.msi |

Windows Installer RelatedProducts detected upstream 1.5.230 on the development
host and no product in the independent companion family. The cached upstream
MSI at `C:\Windows\Installer\7c34.msi` confirms the same three identities.
This is a point-in-time observation; setup must detect again before acting.
Never use Win32_Product, display-name matching or deleting ARP entries.

## Installation state table

| Starting state | Operation | Failure/recovery outcome |
|---|---|---|
| No product, no HidHide resources | Verify fixed installed payload, journal, install signed driver, suspend at MSI reboot boundary, verify and commit after reboot | Roll back newly owned resources; retain protected evidence if cleanup is incomplete |
| Any recognized legacy product | Stop before mutation and instruct normal legacy uninstall | Preserve every product, profile and driver resource unchanged |
| Earlier unified version | Reject downgrade; transactional early old-MSI removal with upgrade-specific driver retention; install new apps | Restore old app package on MSI failure; keep same verified driver; retain maintenance lock until recovery |
| Same unified version | Repair missing/damaged owned resources, avoid healthy-driver reinstall | Report required failure; preserve settings and journal; retry repair |
| Journal or pending reboot | Validate journal schema/ACL/identity, re-detect actual state, resume only verified next step | Unknown/mismatched state stops with journal retained; no blind replay of completed irreversible steps |
| Unknown product, newer/unrecognized driver, orphaned resources | Stop before mutation with exact identities and compatibility diagnostic | Installation preserved; require explicit supported compatibility decision |
| Unified uninstall | Quiesce and restore baseline; detach/verify filters, remove owned nodes/package, remove apps/startup/shortcut | Detachment failure stops before driver deletion; rollback filters if possible; retain recovery data and report failure |

## Transaction, reboot and recovery boundaries

The public MSI never performs nested Windows Installer operations or removes
legacy products. Its immediate impersonated action asks the installed CLI to
restore the initiating user's confirmed baseline and hand off configuration
ownership. An immediate action can receive an elevated consent token during normal
Settings removal. The launcher binds Windows Installer's UserSID to its effective
identity; when elevated, it authenticates CLIENTPROCESSID's token as the same SID,
interactive session and ordinary privilege level. It creates a suspended helper
with that token's environment, verifies its token before resuming, and restores
MSI impersonation afterward. Different-user administrator credentials fail with
a diagnostic rather than selecting a different user's profile context.
Deferred non-impersonating actions accept only a generated transaction
ID, initiating SID, fixed operation and authenticated helper PID; fixed Program
Files and ProgramData locations are independently derived and verified.

Journal schema 1 lives under a fixed `%ProgramData%\mikeev261\HidHide\Maintenance`
directory with SYSTEM/Administrators write access only. Reject reparse points,
unexpected ACLs, unknown versions and oversized data. Use durable atomic replace
and a transaction GUID. Record initiating SID, exact detected ProductCodes and
versions, manifest digest, baseline backup identifier, intended operation, each
completed step and reboot state. Paths are fixed/derived from validated IDs;
no elevated arbitrary command/path fields are accepted.

The historical `mikeev261` maintenance path is an internal persisted namespace.
It remains fixed so in-progress upgrades and recovery journals stay discoverable;
it is not the displayed product or publisher name.

Record intent before each irreversible operation and verify observed state after
it. Fresh install, repair and uninstall use Windows Installer's `ForceReboot`
suspension before `InstallFinalize`. The public package requires interactive MSI
UI so Windows never restarts without a standard prompt. After the user restarts, Windows Installer resumes after that boundary,
the driver worker verifies the new boot and state, and only then may the MSI
commit. Journal completion requires final driver/control-interface, filters and
restored settings verification. Clear temporary hooks only then.

Uninstall is split before CostFinalize: the first request records uninstall intent
but clears REMOVE so MSI retains application files, product registration and its
cached package through ForceReboot. A full REMOVE=ALL would queue ProductUnregister
at InstallInitialize and invalidate its own reboot continuation. On continuation,
the protected uninstall marker requests REMOVE=ALL; the deferred action still
validates journal ownership, operation and boot before finishing. An MSI retry
must not roll back a native transaction started in an earlier attempt. The reboot
condition excludes old-product upgrade removal, recovery and AFTERREBOOT.
Maintenance exceptions produce both a standard MSI error message and log entry.

Driver setup uses bounded, logged Windows driver APIs or a pinned helper after
its source/exit behavior is verified. Enumerate matching root nodes before create;
never blindly create duplicates. Preserve unrelated filter entries and ordering,
add HidHide idempotently and remove only its entries. Read back all three classes.
Detach all applicable filters before removing service/package. A timeout is an
unknown outcome requiring re-detection. Never remove a package by broad name
search or bypass signature enforcement. Same-payload repair skips reinstall when
device, package, service, filters and control interface are healthy.

## Settings and coordinator maintenance

For a fresh profiles-first install, the initiating ordinary user's versioned JSON
repository under `%LOCALAPPDATA%\HidHide Profiles\Profiles` is authoritative for
profiles, selected Global, mode, pause, startup preference, and global Allowed apps.
Do not migrate or depend on `ConfigurationV1`. HKCU retains only driver-state crash
recovery evidence; an active profile's effective blacklist is not baseline. Do not
load arbitrary other-user hives, treat elevated administrator HKCU as the initiating
user's data, or access LocalAppData profile JSON from protected setup actions.

`Shared/ConfigurationChannel.h` authenticates both pipe peers by equal user SID.
The pipe is `HidHide.AppProfiles.Configuration.v1`; the global ownership mutex is
`HidHide.AppProfiles.Coordinator.v1`. Maintenance must retain this authentication.
Use a narrow same-user prepare/shutdown request before elevation and a machine
maintenance barrier checked by all configuration writers. Tray-close is not exit.
Manager restoration failure or ownership conflict prevents maintenance; killing
the manager is not an acceptable handshake. A missing manager still requires
recovery-journal validation in its ordinary user's context. Other-user ownership
blocks setup until that owner cooperates. Elevated actions must independently
hold exclusion during mutations; a user-writable marker cannot authorize them.

Update only owned startup entries in the initiating user's context after the new
path is valid; preserve startup preference. Other users repair their own startup
path at ordinary-user launch. Never launch a resident elevated or SYSTEM GUI.
For uninstall, release the ordinary-user helper only after the protected native
checkpoint is recorded. It removes only exact current/legacy owned Run commands
after confirming the matching uninstall/restart marker, without reading profile
JSON or changing the saved startup preference. Failure release alone is not
authorization to remove startup commands.

## Gates and scope

Runtime safeguards above remain implementation requirements, not verified
behavior. The release gate inspects standard MSI dialog tables, public ARP
visibility, custom-action privilege/sequencing and the reboot continuation around
the protected driver actions. Native dependency inspection must also establish
offline MFC/VC runtime deployment.

The user subsequently authorized testing on this Windows 11 x64 host and explicitly
authorized agent-initiated restarts after configuring Codex auto-start. Capture
recovery material before any lifecycle change. A host migration cannot prove clean/offline,
fault-injection or full VM matrix. Report those separately and do not deliberately
damage the user's input driver to simulate disposable-environment tests. Release
validation now also uses an isolated Windows 11 evaluation VM with a clean
checkpoint, Secure Boot, virtual TPM, and disconnected network.
