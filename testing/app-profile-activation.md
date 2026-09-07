# App profile activation validation

Status: **not run against an installed signed driver**. Source inspection confirms the discovery worker waits 500 ms
between scans (`HidHideClient/src/ProfileManager.cpp`), results are consumed on a 100 ms UI timer
(`HidHideClient/src/HidHideClientDlg.cpp`), and the repository driver gates device opens
(`HidHide/src/Logic.c`, `OnDeviceFileCreate`). These findings are not measurements of a shipped signed binary.

The supported contract is best-effort automatic discovery. Use permanent Devices selections, enabled hiding, and a
feeder whitelist configured before application startup when early opens must be blocked. A detected/running profile
is not proof that a previously opened device handle has lost access.

## Manual procedure (requires a dedicated test machine)

Record the installed signed-driver version, signature, Windows version, physical device/interface instance paths,
and client revision. Preserve the original configuration for restoration. Use a non-whitelisted test application that
logs device-open times/results and can keep a successful handle open while repeatedly reading input. Keep inverse
mode off. Record configuration state and actual input results, not only the profile status label.

1. **Permanent hiding before startup:** Select the physical device on Devices, enable hiding, and whitelist the feeder
   on Applications. Reconnect the device as directed by the client. Start the test application and feeder. Verify that
   the application's first open is denied while the feeder can open and read the physical device.
2. **Profile-only early open:** Remove that device from permanent selections, keep hiding enabled, and configure a
   profile for the test application. Start it so that it opens the device immediately. Repeat launches to exercise the
   discovery window. Log whether an open succeeds before the manager detects the process; do not assume a fixed delay
   will reliably reproduce the race. A successful early open is consistent with the best-effort contract.
3. **Retained handle:** Obtain and keep a successful handle before adding the device to the running application's
   profile. Wait until the configuration actually includes the device. Read using the retained handle and attempt a
   fresh open separately. Record whether old-handle reads continue and whether the fresh open is denied. Do not
   reconnect during this step: that would invalidate the handle and obscure the behavior under test.
4. **Recovery by startup ordering:** Close the test application, configure permanent hiding as in step 1, then launch
   it again. Verify its first open is denied. Repeat with the manager starting at sign-in to ensure the permanent
   selections, rather than discovery timing, provide the preexisting policy.

Restore the original configuration after testing. Record pass/fail, timestamps, and observations for every case,
including any divergence between the installed driver and repository source. Do not mark signed-driver validation
complete based on compilation or source-level tests alone.
