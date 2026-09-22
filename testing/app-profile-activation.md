# App profile activation validation

Status: **not run against an installed supported signed driver**. Source inspection
and the isolated `Editor/tests/launch-order.mjs` fixture establish the direct
process ordering in the client. They do not establish physical-device behavior of
the installed Microsoft-signed driver or whether retained handles continue to read.

The supported contract has two ordered paths. A verified **Use Global** profile
can hide physical devices before an external game launch. **Launch with profile**
creates the saved application's exact executable suspended, applies and reads back
its complete policy, then resumes it and returns to Automatic with no manual pin.
The launched process participates in normal newest-activation selection. Automatic discovery after an external launch remains best effort. Applying
an inactive application profile only saves it. The direct action cannot guarantee
startup ordering for a launcher that hands off to another process, child processes
after the tracked parent exits, an already-running copy, or a game requiring extra
launch arguments. It refuses an existing target, an Allowed-app target, paused
hiding, stale saved versions, maintenance, and unknown or conflicting driver state.

## Dedicated-machine procedure

Use a disposable supported Windows 11 x64 machine with the exact verified
Microsoft-signed upstream driver and current client installed. Record the driver
version/signature, client revision, physical device/interface identities, feeder
path, test executable path, and timestamps. Back up the profile repository and
record the observed baseline before changing policy. Use a non-Allowed test
application that logs open and read results and can hold a successful handle.
Do not run this procedure against the development host's existing user profile.

1. In the editor, save a Global profile with the physical device Hidden. Put the
   feeder executable in **Allowed apps** and keep the test executable out. Select
   **Use Global**, Apply, and confirm the Global profile and driver state show
   verified. Reconnect the device if directed. Start the test executable externally.
   Record its first open result and verify that the feeder can still open and read.
2. Configure a visible Global fallback and a saved enabled application profile
   that hides the physical device. Use **Automatic**. Start the test executable
   externally so it attempts an immediate open before discovery. Record the open
   time, first result, profile activation time, and any later reads. Repeat enough
   times to observe the timing window; do not infer a guarantee from a pass.
3. Close every copy of the test application. Confirm the Global fallback is
   visible and the application profile is saved but inactive. Use **Launch with
   profile**. Record the first open, the applied/verified state before launch,
   feeder access, and the active mask while the tracked process runs. Start a second
   saved application and verify it takes over; close it and verify fallback to the
   still-running launched application. Pin an older running mask, start another
   application, then Return to Automatic and verify the newest surviving activation. Confirm
   an already-running copy is rejected. Close the launched process and verify a
   fresh automatic scan returns to the selected fallback.
4. For retained-handle behavior, deliberately obtain and keep a successful
   handle while the device is visible. Change the active policy to Hidden and
   verify the driver state. Compare reads through that old handle with a fresh
   open. Do not reconnect during this step, since reconnecting would invalidate
   the handle and obscure the result.
5. Close the test application, restore the original profiles/settings and
   baseline through the normal UI or recovery flow, and verify the original
   feeder exemption and device behavior. Retain logs and exact outcome for each
   step, including any divergence from repository-source expectations.

The signed-driver and retained-handle gate remains open until this procedure is
run with an authorized physical device and the evidence is reviewed. Compilation,
mock enforcement, and a real suspended child with no physical driver do not close it.

Process-discovery regressions also run without the installed driver. The native
`ProcessLifetimeCache` tests repeatedly start the native test executable with only
its inert `ChildExitProbe` test selected, exercise real retained handles through
exit, and keep an inaccessible candidate in every synthetic scan. Other cache tests
reject thousands of wrong-path candidates and preserve overlapping handoff versus
separate activation episodes. `Editor/tests/recent-mask.mjs` verifies saved Pause
and Use Global, saved edits, Retry, and worker reconciliation during incomplete
discovery, then checks that returning to Automatic does not fabricate transitions.

Only live exact-match process objects are normally retained. Exited lifetimes
become a per-path union of time intervals and their handles close immediately,
independent of scan completeness. Incomplete scans never rescan that exit history;
overlapping intervals merge on insertion. A complete scan consumes the evidence,
and disabled, deleted, missing or changed paths discard it. Under indefinitely
incomplete discovery, distinct observed inactive gaps can still require one compact
interval each until discovery recovers. There is no arbitrary truncation that could
silently turn an observed handoff into a fresh activation.
