# HidHide Profiles 2.1.13 performance review

This review concerns the existing source checkout and isolated editor fixture. The installed application has not been changed. Timings below will distinguish the fixture from live driver behavior.

## Ranked findings before changes

1. **Native request latency, every command:** `CHidHideClientDlg::ChannelWorkerMain` waits 500 ms on `m_ChannelStop` before calling either pipe server's `Pump`. A request can sit idle for nearly half a second; every response then triggers another synchronous `ObserveEnforcement` and tray refresh on the UI thread. The configuration and editor pipes share that worker.
2. **Process creation, every editor command:** `Editor/electron/main.cjs` starts a fresh `HidHideClient.exe --editor-request` and waits for it to exit. Repeated snapshots pay Windows process startup, C++ runtime/MFC load, token checks, standard-stream setup and pipe negotiation each time. The bridge remains authenticated, so a persistent ordinary-user bridge is the appropriate improvement.
3. **Duplicate startup snapshot:** `Editor/electron/main.cjs` sends a second snapshot after `window.loadURL` even though the renderer loads one. This doubles cold backend work and can race initial rendering. A direct editor launch still needs bounded engine startup/reconnect behavior.
4. **Snapshot work under UI-thread dispatch:** `EditorService::Snapshot` takes a repository writer lease, loads the repository, parses each saved profile and settings again for equality, hashes each file, observes driver state and builds the entire device array. `OnChannelRequest` then observes driver state again. Hash/version correctness must remain; avoid repeated work and duplicate observation while preserving external-change detection.
5. **Renderer polling starvation:** the fixed 1800 ms snapshot timer can queue a new poll while the prior poll is still pending. The existing sequence guard correctly rejects old results, but under persistent responses slower than 1800 ms each queued poll can invalidate its predecessor before it renders, leaving the screen in a connecting state. The renderer now coalesces refreshes and respects native window visibility.

## Changes in the 2.1.13 source candidate

- The native configuration/editor pipe now uses overlapped connect, read and write events. The worker waits on those events and its shutdown handle rather than sleeping 500 ms before each pump. A stalled client has a four-second deadline; cancellation is drained before request/response buffers are destroyed. The editor pipe checks the impersonated client's SID, interactive session and non-elevated token before dispatch. The historical configuration/CLI pipe keeps its existing admission contract.
- Electron keeps one ordinary-user native `--editor-session` helper for serialized commands, with 15-second request timeout, bounded line framing and cleanup on editor exit. The duplicate post-load startup snapshot is gone. A direct editor launch can still start the fixed sibling engine once when its first snapshot fails.
- The native launcher starts Electron after driver-presence and owner admission, while repository/recovery initialization continues. `--engine-launching` keeps this path from starting another owner during connection. The coordinator reuses the initial enforcement observation. Snapshots reuse the parsed repository versions and hashes from the same load under the writer lease, and editor requests no longer perform a second driver observation after dispatch. Device responses include presentation-only kind metadata.
- The renderer now uses single-flight adaptive polling, an honest connecting state, native visibility handling and memoized device/rule projection. A repository-only unchanged token would be unsafe because process selection, device connections and driver observation change independently of JSON generation.

## Measurements and checks

The repeatable `Editor/tests/transport-benchmark.mjs` uses a new direct TEMP fixture root, a fake enforcement adapter and a fake three-device source. These numbers are **not installed-driver or production startup measurements**. Snapshot response size changed from 2,098 to 2,149 bytes because of device kind metadata. Each latency run used serial requests on this host; p90 is the nearest-rank sample.
Captured benchmark output is in `artifacts/performance-ui-validation/native-transport-measurements.json`; the original pre-change executable was subsequently replaced by the rebuilt candidate.

| Fixture path | Requests | Median | p90 | Range |
| --- | ---: | ---: | ---: | ---: |
| Before: 2.1.12 one-shot helper, fixture's 50 ms poll | 20 | 188.76 ms | 189.91 ms | 67.46–191.62 ms |
| After: event-driven host, one-shot compatibility helper | 20 | 13.29 ms | 14.26 ms | 12.14–16.53 ms |
| After: event-driven host, persistent editor session | 30 | 0.57 ms | 0.68 ms | 0.47–8.88 ms |

The full isolated native/Electron test passed five integration groups: Apply/CAS and observed policy, editor process cleanup, automatic switching with no editor, reconnect, and honest unknown state after engine shutdown. It now asserts that the persistent native helper PID exits with every Electron close. Five editor process launches to the first usable fixture snapshot took 270.5, 240.4, 233.8, 243.0 and 226.7 ms; the first run from starting the fixture host to that snapshot took 398.2 ms. These use development Electron, a warm host and local assets; there is no comparable pre-change end-to-end sample.

The same fake-driver fixture retained its native host after the GUI closed. Over a separate five-second idle interval, the host accrued 0 ms measured CPU time, with 13,332,480 bytes working set and 3,448,832 bytes private memory. The GUI had four Electron PIDs while open; all four and the editor-only native helper exited on close. Native Release x64 and test projects built, 29 focused channel/repository tests passed (including stalled-client recovery and pending-I/O teardown), and native/Electron integration passed under the interactive Windows user. The sandboxed Electron renderer could not start on this host; the same isolated test passed outside the sandbox.

## Startup and remaining boundaries

Before this change, native `InitInstance` admitted the owner and checked driver presence, then `OnInitDialog` synchronously constructed the proxy, inspected driver recovery/observed state, opened and inspected the repository, installed the configuration channel and tray, and only then launched Electron from a posted message. The new overlap removes that serial dependency from visible editor startup. The remaining driver reads and recovery checks preserve verification; their cost on the installed 2.1.12 host has not been measured safely. The installed application was inspected read-only and was not replaced or launched for this review. Exact 2.1.13 installed-driver, hardware, cold-boot and MSI lifecycle latency remains a separate acceptance boundary.

## UI presentation and independent review

The reusable Sol definition is `.codex/agents/hidhide-performance.toml`; an explicitly
configured gpt-5.6-sol implementation agent followed it, with a separate Sol reviewer.
Independent review found and repaired the pending native I/O teardown above, stale
icon fallback after missing executables appear, and an initial visibility query that
could override a later show event. Final source review reported no remaining
actionable findings in the changed UI, transport, admission and version/CAS paths.
This is a review result, not a guarantee that every possible defect is absent.

Actual installed LMU executable artwork was read and rendered. Wheels, pedals,
Stream Decks, cameras, audio devices and generic/unknown devices have distinct
category artwork. Native product and usage metadata are used without extra device
enumeration. Missing metadata retains a neutral icon. Hide disconnected persists
locally and preserves saved rules, pending edits and unknown connection states.

Seven model/icon test groups, thirteen Electron UI acceptance groups, and five
icon/filter presentation groups passed. The UI tests include resize/maximize and
125-200 percent zoom. The polling regression injected 2300 ms snapshots and reached
the first usable snapshot in 2380 ms, with peak concurrency one, zero hidden-window
requests, successful resume and a deliberately delayed initial visibility reply.
Evidence: `artifacts/performance-ui-validation/{polling,presentation}.json`,
`icons-and-filter.png`, and `artifacts/electron-ui-validation/native-integration.json`.

Agent configuration follows the [official Codex subagent format](https://learn.chatgpt.com/docs/agent-configuration/subagents).
Executable artwork uses Electron's [app.getFileIcon API](https://www.electronjs.org/docs/latest/api/app#appgetfileiconpath-options).
