# HidHide Developer Guide

## Programmatic integration

Applications that need to communicate with *HidHide* at runtime can do so through a WDM control device that the driver
exposes under the symbolic link `\\.\HidHide` (DOS device name). The same device can alternatively be opened by the
interface GUID `{0C320FF7-BD9B-42B6-BDAF-49FEB9C91649}` via `SetupDiGetClassDevs` or `CM_Get_Device_Interface_List`.

Once opened, the device accepts a set of I/O control codes constructed with `CTL_CODE(32769, N, METHOD_BUFFERED,
FILE_READ_DATA)`. Function numbers 2048 through 2055 correspond to the get and set operations for the whitelist,
blacklist, active state, and whitelist-inverse flag respectively. List payloads are exchanged as `MULTI_SZ` buffers —
a sequence of null-terminated wide-character strings with an additional null terminator to close the list — and the
buffer size supplied to `DeviceIoControl` must be an even number of bytes and include all terminators. Device instance
paths should be in the format returned by `SetupDiGetDeviceInstanceId`, for example
`HID\VID_054C&PID_09CC&MI_03\7&...`.

Functions 2056 and 2057 expose the *session blacklist*, which is intended for feeder applications — programs that
exclusively own one or more physical devices and re-expose them as virtual controllers. Unlike the persistent blacklist
managed through functions 2050 and 2051, session blacklist entries live only in kernel memory and are never written to
the registry, leaving the user's permanent *HidHide* configuration untouched. Each entry is owned by the process that
issued the `ADD_SESSION_BLACKLIST` request. When that process exits for any reason — clean shutdown, crash, or forced
termination — the driver automatically removes all of its entries within its process-notify callback registered via
`PsSetCreateProcessNotifyRoutine`, so no cleanup code is required in the application. On a clean exit one may optionally issue `CLR_SESSION_BLACKLIST` to release the
devices immediately, but this is not required. This approach is preferable to temporarily mutating the persistent
blacklist because it requires no rollback logic and is inherently safe against unexpected termination.
