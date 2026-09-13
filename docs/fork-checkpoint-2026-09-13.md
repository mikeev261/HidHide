# HidHide fork checkpoint and next-agent handoff

Checkpoint created September 13, 2026. [Draft PR #21](https://github.com/mikeev261/HidHide/pull/21) targets `master` from `codex/unified-hidhide`. The last runtime validation was September 8; this checkpoint does not claim a new machine-health check on September 13.

The fork is at **2.1.1.0**. Its practical local installation, upgrade, repair, reopening and automatic profile state changes have been tested. It is not a fully qualified public release. The user explicitly chose a generally working, unsigned build tested on this local Windows 11 PC and cancelled further VM work. No merge or release publication is authorized by this checkpoint request.

## What upstream supplies and what the fork adds

Upstream HidHide supplies the device-hiding driver, application whitelist/inverse mode, a configuration utility, and CLI. Its essential behavior is access control for selected physical HID devices: ordinary applications cannot open hidden devices, while whitelisted feeder/configuration programs can. This prevents games from receiving both physical-controller and virtual-controller input when a feeder is used.

This fork builds on those capabilities with a resident application-profile manager, safer shared configuration handling, a revised device-selection UI, and an independently owned unified installation/recovery system. These are user-mode and packaging additions. The supported package uses the exact unchanged Microsoft-signed upstream driver; it does not require a custom kernel build or new IOCTLs.

| Area | Fork addition |
|---|---|
| Automatic profiles | Associate exact executable paths with devices; detect running applications and temporarily combine their device selections. |
| Resident operation | Keep monitoring from the tray when the window closes, register ordinary-user sign-in startup, reopen the existing UI, and explicitly exit with baseline restoration. |
| Configuration ownership | Give GUI and CLI a shared, authenticated coordinator; distinguish saved baseline intent from temporary effective driver state. |
| Recovery and conflicts | Record recovery state before overrides, preserve uncertain outcomes, and require explicit acceptance of conflicting external edits. |
| Device selection | Share physical-device grouping and interface-level selection across Devices and App Profiles, with filters, refresh handling and selection preservation. |
| Unified deployment | Ship one Windows 11 x64 Burn EXE, one private MSI and one visible fork entry, including apps, runtimes and verified upstream driver media. |
| Lifecycle support | Add protected preparation, driver install/repair/removal, recognized migration, versioned upgrades, reboot continuation and recovery protocols. |
| Build evidence | Centralize product versioning, verify payload identity and architecture, produce source/hash manifests, and run native, managed and MSI structure checks. |

Some profile/coordinator work predates this installation session. The PR includes it because it is also absent from the current `master`. The unified-package session began from `4efeeabd038b2ff75929eed2df9c7cdf4b95947d`, with the earlier PR #20 work already present. Do not describe every line of the PR as newly written during the latest installer fixes.

## How App Profiles actually work

Each profile identifies an executable by its full path and stores a set of device instance paths. Matching uses the process image path, not merely a filename such as `game.exe`. Process polling runs off the UI thread. The UI exposes running/unresolved status and physical devices that can be expanded for individual HID interfaces.

The manager preserves a baseline: the user's normal Devices-tab selections, enabled/inverse state and whitelist. While profiles run, their device sets are combined with the permanent selections. Active profile contributions enable hiding temporarily. When the contributions disappear, the original baseline is restored. Multiple profiles can overlap without removing another profile's or the baseline's selections.

This is **global hiding triggered by an application**, not kernel filtering scoped exclusively to that application. The signed driver has one global hidden-device list. Every non-whitelisted application is affected while a profile is active. Feeders must stay whitelisted to read hidden physical devices; a game's membership on the whitelist also affects what it can see.

Detection is best effort. It does not guarantee hiding before a game opens its controller handles and does not revoke handles already opened. For dependable startup hiding, configure permanent hiding and feeder whitelisting before launching the game. There is no guaranteed apply-profile-then-launch workflow.

Closing the main window keeps monitoring in the tray. The tray provides pause/restore, resume, explicit conflict acceptance, and Exit and restore device settings. Explicitly disabling hiding suspends automatic profiles until resumed, including across restarts. Pause and disable preserve baseline intent rather than silently replacing it with a temporary override.

The CLI retains ordinary configuration commands and adds profile list/create/delete/add/remove operations. Its configuration reads and edits represent baseline intent while the coordinator is resident; they are not a substitute for reading the driver's effective state during a profile test. Read-only queries do not implicitly whitelist the CLI.

## Configuration integrity and ownership

Only one coordinator can own automatic profiles across Windows sessions. GUI and CLI use a bounded, versioned named-pipe protocol with same-user authentication and compare-and-commit behavior. Configuration operations open the driver for a transaction, read fresh state, verify expected state, apply changes and read back. They do not retain a control handle indefinitely.

The coordinator distinguishes its expected effective state from baseline intent. Unexpected external changes during an override stop reconciliation and retain recovery evidence. The user can explicitly adopt current driver settings as the new baseline and remain paused. Failed restoration is not reported as successful; exit can remain resident to expose the failure. The design does not pretend a sequence of driver IOCTLs is atomic.

Useful code: `Shared/Configuration.h`, `Shared/ConfigurationChannel.h`, `HidHideCLI/src/ConfigurationSession.h`, `HidHideCLI/src/FilterDriverProxy.cpp`, and `HidHideClient/src/ProfileManager.cpp`. Behavioral documentation is in `CONFIGURATION_OWNERSHIP.md` and `README/configuration-state.md`.

## What the unified installer adds

The earlier companion-only packaging arrangement is superseded. The current supported target is Windows 11 x64. `%ProgramFiles%\HidHide` contains the GUI, CLI, app-local VC/MFC runtimes and a Driver subdirectory with verified INF/SYS/CAT/license. Setup owns the actual driver lifecycle as well as application installation. The bundle supplies the single visible Installed Apps entry; the MSI is private.

The upstream release EXE is version 1.5.230, but the actual driver inside it is **1.4.181.0**. The fork app/setup version is independent. The installer verifies pinned hashes, catalog trust and membership, architecture, version and original license. Owned setup/application code is explicitly unsigned for this checkpoint. Optional code-signing support exists, but its certificate-backed path has not been exercised here. The kernel payload remains Microsoft-signed.

| Component | Responsibility |
|---|---|
| `Installer.Bootstrapper` | Ordinary-user Burn orchestration, UI/progress, initiating-user preparation, elevation launch and completion/startup handling. |
| `Installer.Controller` | Protected cache/journals, peer authentication, preparation/recovery, recognized legacy removal outside the MSI transaction, and final verification. |
| `Installer.Driver` | Native driver state inspection and lifecycle steps, filter preservation, settings restoration, reboot identity, journals and rollback. |
| `Installer` | Private MSI authoring, product/version/architecture contracts, custom-action bridge and signing support. |

Before mutation, setup asks the ordinary-user coordinator to restore and confirm baseline, then takes maintenance exclusion. The elevated controller verifies the initiating peer, protected cache and actual state. Journals record intent before irreversible steps. Completion requires checking application bytes, registration, driver, filters and restored settings. A 3010 result requires a reboot and continuation with the **same exact setup**; it is not a completed operational-success result.

Recognized legacy/upstream and companion migration, compatible unified upgrades, repair, damaged-driver reconstruction, rollback and recovery are implemented with conservative identity/state checks. Complete recovery media must exist before relevant legacy removals. Unknown/newer/unreconciled state stops rather than guessing. These implemented paths have different validation levels; see the table below before treating any one as proven.

The supported build excludes archival kernel and obsolete watchdog/package paths. No WDK or ARM64 toolchain is needed for the x64 product. Historical source remains reference material, not the shipped driver implementation.

## Most recent changes and fixes

| Commit | Change and evidence |
|---|---|
| `85bce35` | Fixed reopening a running coordinator: the second ordinary launch previously opened the existing window **and** showed an ownership warning. It now finds a marked window belonging to the same user/session, requests activation and requires acknowledgement. Missing/unresponsive owners still produce a warning. Also prevents queued startup-hide from hiding a window just reopened. Four new native tests passed; installed reopening passed before and after repair. Version advanced to 2.1.1.0 for a distinct upgrade. |
| `64ef587` | Treat native elevation-launch cancellation (1223 at the launch boundary) as setup cancellation (1602) with an explicit explanation. Other failures remain failures. Existing recovery state is retained. |
| `51377b5` | Validate the PE architecture of both staged owned executables before MSI generation. Malformed/truncated/incompatible images are rejected; version labels alone cannot establish x64 compatibility. |
| `26a8d50` | Consolidated the unified installation/recovery implementation and fixes found during the native testing described below. |
| `59e0ce2` and intervening documentation commits | Recorded exact artifact identities, completed local/earlier VM checks, changed testing scope, startup restoration and remaining acceptance gaps. |

Earlier installer-debugging fixes included a valid independently pumped Burn parent window, removal of invalid deferred MSI `SetMode` usage, explicit reboot/pending-service-deletion handling, correct empty `REG_MULTI_SZ` decoding, and controlled handling/removal of exact pinned residual driver files. Diagnostics were improved so controller failures were not simply lost as a disconnected peer. The GUI coordinator lease lifetime was corrected after debugger evidence of double destruction during shutdown. Earlier normal exit and maintenance handoff were retested under the debugger.

These are substantive fixes to reproduced failures, but the final artifact was not subjected to every historical failure scenario again. Historical checkpoint documents intentionally retain earlier failures and incomplete states.

## Exact artifact and validation boundary

Final tested application source: `85bce353ce5c9100e9b154aeed30c1610a3360fe`.

Local setup:

```text
C:\Code\HidHide\artifacts\local-release-validation-85bce35\artifacts\local-test-candidate\HidHide_2.1.1_x64.exe
SHA256 3D2FEB6F1C6B31FEC54FC013819820BFBA77880E04984C1A5FE8F5E276B481AC
```

The hash was rechecked when writing this handoff. Its manifest records clean source, unsigned owned code and all three recovery media. Subsequent documentation-only commits do not change the frozen artifact's source identity.

| Check | Evidence and limit |
|---|---|
| Clean build/package | Unified Ci passed: 95 native tests; 61 installer, 188 driver, 144 controller and 65 bootstrapper checks, plus supporting security/recovery/provenance checks. Full unsigned packaging and 45 private MSI structure checks passed. |
| Ordinary removal | Old 2.0 uninstall/reboot/resume returned 0 and reached strict empty product/driver/filter state. Final 2.1.1 standalone uninstall was not repeated. |
| Install/reboot | 2.1 install/reboot/continuation returned 0, committed its journals and cleared the marker. |
| Final upgrade/repair | 2.1.1 upgrade and same-artifact repair returned 0 without reboot. Final inventory showed one visible product, healthy exact signed driver, Secure Boot enabled and no pending maintenance. |
| GUI | Applications/Devices/App Profiles inspected, connected controllers enumerated. Final About showed correct fork/driver versions and credits. Final second launch exited 0 and opened the existing window with no ownership popup, including after repair. |
| Profile behavior | On repaired 2.1.1, a temporary bounded process profile applied exactly one joystick interface in the actual effective driver state, then restored the exact baseline and preserved original profiles. This does not prove in-game device-open timing or controller input. |
| Exit and startup | User exercised normal tray Exit on 2.1; process ended, baseline remained restored and no HidHide crash event was found. Final 2.1.1 normal exit was not repeated. The user's enabled startup preference was restored to the canonical installed path. |
| Refused maintenance | One final repair attempt was refused before mutation because About was open. Closing About and retrying succeeded. Close child dialogs before running setup; the failed attempt remains in the evidence. |
| Earlier VM | An older candidate passed offline install/reboot/resume and repair. This is separate evidence, not final-artifact acceptance. User cancelled further VM testing. |

Local evidence is under `artifacts/local-validation-20260908`, especially `host-211-final.json`, `upgrade-211-result.json`, `repair-211-retry-result.json`, `final-211/profile-result.json`, and `final-211/final-user-state.json`. Build logs and the manifest are under the frozen candidate worktree. Generated artifacts and raw user/machine recovery evidence are local and are not committed to the PR.

## Instructions for the next agent

1. Read `AGENTS.md`, `INSTALL_LAYOUT.md`, `MAINTENANCE.md`, `docs/unified-package-design.md`, this handoff and `docs/unified-package-progress.md`. Earlier documents titled “current checkpoint” may describe historical recovery states. Do not replay them as today's instructions.
2. Treat September 8's completed local state as the last verified state, not a live observation. Inspect current state before any new lifecycle action. No restart was pending at that checkpoint.
3. Preserve the exact installed setup and protected recovery journals. Do not replace it with a rebuilt same-version bundle. Change the MSI-compared product version for a new upgrade and preserve artifact provenance.
4. Preserve profiles, baseline, unrelated filters and feeder whitelist entries. Do not run the resident GUI as SYSTEM/elevated. The sandbox account has a different HKCU from the real user; use the actual user's ordinary session for settings and app launches.
5. Do not modify kernel code, exported IOCTL contracts, signed payload, Secure Boot, trust stores or test signing. Never clear journals/maintenance markers or delete uninstall registrations to manufacture a clean state.
6. The practical local-use scope is complete enough for the user's chosen checkpoint. Actual game input, final-version standalone uninstall/normal exit, exhaustive legacy combinations, cross-user/alternate-admin behavior, damaged-driver and interruption/power-loss recovery remain untested. Revisit these only as needed for the next requested scope; do not restart the cancelled VM effort by default.
7. If preparing a public release later, reconcile `docs/release-readiness.md` against this newer evidence. It is an older broader audit, not a declaration that all those gates now pass. Preserve the distinction between unit checks, actual native lifecycle evidence and public readiness.
8. Three unrelated pre-existing untracked files remain: `TrustTestCert.ps1`, `nefcon.zip`, `nefcon_out.txt`. They were deliberately excluded from the PR and are not trusted installer inputs. Leave them alone unless separately requested.

No software release was published and no PR was merged as part of this checkpoint.
