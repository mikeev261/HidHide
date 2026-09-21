# Profile activation contract and device-open ordering

Status: documented design characteristic of the signed HidHide driver and
profile manager architecture.

## How profile-based hiding works

The HidHide driver enforces device visibility at **device-open time**
(`IRP_MJ_CREATE`). When a process calls `CreateFile` on a hidden HID
device, the driver's `OnDeviceFileCreate` callback checks the current
hiding list and allowed applications. If the device is hidden and the
caller is not an allowed application, the open is denied.

The profile manager (`HidHideClient.exe`) runs as a background coordinator
under the interactive user. It periodically scans running processes to
determine which application profile, if any, should be active. When a
matching profile is found, the coordinator writes the corresponding device
visibility policy to the driver via IOCTLs.

## The activation ordering characteristic

Because the driver enforces hiding at open time and the profile manager
discovers running processes via polling, there is a window between when an
application starts and when the coordinator activates its profile:

1. User launches a game.
2. The game opens a controller handle via `CreateFile`.
3. The coordinator's next poll cycle detects the game's process.
4. The coordinator activates the game's profile, updating the driver's
   hiding list.

If step 2 occurs before step 4, the game has already acquired a valid
device handle. The driver does not revoke previously granted handles.

This is a **fundamental characteristic of the current driver architecture**,
not a bug in the profile manager. The signed Microsoft-published driver
uses open-time enforcement by design, and this project does not modify
kernel code, IOCTL contracts, or signed driver payloads.

## Supported activation patterns

### Pattern 1: Persistent Global hiding (recommended for double-input)

The recommended approach for the standard double-input use case:

- Use the **Global** profile to persistently hide game controllers.
- Add feeder applications (SimHub, JoystickGremlin, etc.) to the
  **Allowed apps** list so they can read hidden devices.
- Games never see the hidden devices regardless of startup order.

This pattern is immune to the polling window because the hiding policy
is applied before the game starts, not in response to it.

### Pattern 2: Automatic application profiles (best effort)

Application profiles activate when the coordinator detects a matching
process. This provides convenience but is subject to the polling window:

- If the game opens a controller quickly after launch, it may acquire a
  handle before the profile activates.
- Once the profile is active, subsequent device opens are correctly
  enforced.

**Automatic profiles are best effort.** They work well for games with
delayed device initialization, but they cannot guarantee hiding for
applications that open controllers immediately at startup. For reliable hiding, use persistent Global hiding or the explicit **Launch with profile** flow.

### Pattern 3: Explicit launch workflow

For reliable hiding with application profiles without relying on a Global fallback, use the **Launch with profile** feature provided by the editor UI:

1. Close any running copies of the game.
2. Ensure the desired application profile is saved and enabled in the editor.
3. Use the **Launch with profile** action to start the game directly from the editor.

- This workflow explicitly applies the requested profile's policy to the driver **before** creating the game process.
- Because the driver is configured first, the game encounters the hiding rules immediately during its initial startup.

## What the profile manager does not do

- It does not revoke device handles that were granted before a profile
  change. This would require kernel-level handle invalidation, which is
  outside the scope of the user-mode profile manager.
- It does not inject into game processes to intercept device opens.
- It does not use process creation callbacks (these require a kernel
  driver, which this project does not modify).

## Implications for the editor UI

The editor displays both the **profile rule** (the desired policy) and
the **applied now** state (what the driver currently reports as its active policy).
These may differ when:

- The coordinator has not yet applied a profile change.
- The driver is not installed or not running.

**Important:** The "Applied now" column shows the observed driver policy. It
**does not** prove that an application lost access to a device. If a game
acquired a handle before the profile activated, the game retains effective access
even though "Applied now" correctly reports the device is currently hidden
from new `CreateFile` attempts.
