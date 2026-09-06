# <img src="assets/hidhide-128x128.png" align="left" />HidHide App Profiles

A user-mode companion for an existing [official Microsoft-signed HidHide driver](https://github.com/nefarius/HidHide). This fork provides the configuration UI, resident app profile manager, and CLI; it does not distribute a driver. See [installation layout](INSTALL_LAYOUT.md) and [build, release, and maintenance scope](MAINTENANCE.md).

## Introduction

*Microsoft Windows* offers support for a wide range of human interface devices, like joysticks and game pads.
Associating the buttons and axes of these devices with application specific behavior, such as *Fire*, *Roll*, or *Pitch*
is however left to the individual application developers to realize.

While there are good examples of applications allowing a user to customize the controls to their liking, other
applications are less sophisticated or lack just that feature a user is looking for. This is where utilities like *vJoy*
and *Joystick Gremlin* come to the rescue. These utilities aren't limited by a vendor lock-in and attempt to move
certain features back into the domain of the operating system. Once properly arranged, a feature becomes
universally available for a wide range of applications.

A technique used by these utilities is to use a feeder application that listens to the physical devices on a system,
and in turn controls one or more virtual devices where the game or application is listening to. Mapping physical
devices to a virtual device allows for e.g. dual joystick support in games that only support a single joystick, or
enable multiple devices to bind to the one and same function in a game that only supports single controller bindings.

While this approach offers a lot of advantages, it also comes with a side effect. Most applications record the user
interactions while binding a function with a control or button press. When a virtual device is used, the application
receives input from two devices simultaneously. It will be notified by both the physical device triggered, and the
virtual device that acts in turn! Some feeders have an option to spam the application repeatedly; however, that approach is
cumbersome and error prone.

With *HidHide* it is possible to deny a specific application access to one or more human interface devices, effectively
hiding a device from the application. When a HOTAS is preferred for a flight-simulator one can hide the game pads.
When a steering wheel is preferred for a racing game, one can hide the joysticks, and so on. When, as mentioned
above, a feeder utility is used, one can use *HidHide* to hide the physical device from the application, hence avoiding
multiple notifications while binding game functions and device controls.

## Package content

The companion MSI installs `HidHideClient.exe` and `HidHideCLI.exe` under `%ProgramFiles%\HidHide App Profiles\`. Install the official HidHide driver separately first. The companion never installs, replaces, or removes driver files or services. The configuration utility runs without elevated rights.

## User guide

The configuration utility allows you to:

- Enable or disable the service
- Specify which applications may look through the cloak
- Specify the human interface devices that should be hidden from ordinary applications

The main dialog of the configuration utility offers three main tabs.
![Screen capture of applications tab](/README/DlgApplications.jpg)

The *Applications* tab shows all white-listed applications that are allowed access to the hidden devices. Typically listed
here are vendor-specific utilities for configuring the human interface devices, and feeder utilities. Entries can be added
to the list by pressing *+*. Select one or more entries with the *shift* and/or *control* key and press *-* to remove entries
from the list. Notice that the client replaces a logical drive letter by a full path. This is intentionally and offers some
resilience for changes in logical drive mapping.

![Screen capture of devices tab](/README/DlgDevices.jpg)

Per default, the *Devices* tab lists all *Gaming devices* currently connected to the system. The list refreshes automatically
when a new device is detected. The dialog offers two check boxes for filtering.

Via *Filter-out disconnected* one can extend the list with devices that were connected earlier to the system but are
currently not present. With *Gaming devices only* one can limit the list to game pads and joysticks only. This feature
relies on proper information from the device vendors. Some vendors however use vendor-specific codes. Be sure to
switch off this filter should you notice that your gaming device seems absent in the list. The filters are ignored for
devices that are selected for hiding, so that one has a complete overview on the hidden devices.

Last but not least, the *Enable device hiding* check box provides control over the *HidHide* service. When enabled it
blocks access to the black-listed devices unless the application is explicitly white-listed. When disabled, all applications
are granted access to all devices.

An entry in the list can be expanded to reveal the composite devices associated with a device and offers fine-grained
control over a device. *HidHide* uses the selection also for a secondary purpose. Some legacy applications ignore the
human interface device layer offered by the operating system and instead interact with the underlying device driver.
Access to the underlying driver will be blocked when a device only has composite HID devices, and all are selected.

The expanded list may mark entries as *absent* or *denied*. *absent* entries appear when the device characteristics are altered.
These are residual entries in the caches of the operating system, and can be cleaned-up using utilities like *Device Cleanup Tool*.
*denied* entries appear for hidden devices when the configuration utility itself is not whitelisted.

The *App Profiles* tab adds hidden devices when it detects a configured executable running. Automatic detection is
**best effort**; it does not guarantee hiding before an application opens a device. Add an executable
with *+* (or drag it onto the tab) and select the physical devices to hide. Closing the window leaves the profile manager
running in the notification area; use its tray menu to reopen it or exit and restore the normal Devices-tab configuration.
The manager starts automatically at sign-in whenever profiles are configured. If multiple profiled applications run at the
same time, their selected devices are combined. This user-mode design reuses the separately installed Microsoft-signed
HidHide driver and remains compatible with Secure Boot.

The signed driver exposes a single global hidden-device list, so an active profile temporarily affects every non-whitelisted
application, not only the executable that activated it. The manager preserves the normal device list, adds active-profile
devices to it, restores it when the last profile exits, and records recovery data before each override. An application on
the *Applications* whitelist retains access to every hidden device.

For reliable hiding at application startup, select the physical devices permanently on the *Devices* tab, turn on
*Enable device hiding*, and whitelist feeder utilities on *Applications* (with inverse mode off) **before starting the
game**. Keep the game off the whitelist. Reconnect devices after configuration changes as directed on the Devices tab.
Profiles supplement these permanent selections; they do not replace them.

The manager scans processes approximately every 500 ms and consumes results on a 100 ms UI timer. These intervals are
not a deadline: scheduling, configuration dialogs, and errors can delay application of a detected profile. Starting the
manager first, automatic sign-in startup, or a profile showing *Running* does not establish that hiding preceded a game's
first device open. The repository driver checks access at device-open time and does not revoke already-open handles.
An application that opened a controller before hiding took effect can therefore retain access. Close that application,
configure permanent hiding, and then restart it; waiting for detection does not repair an existing handle. There is no
apply-profile-then-launch workflow in this client.

This activation contract is based on source inspection, not live verification against an installed signed driver.
The [manual validation procedure](testing/app-profile-activation.md) covers startup ordering and retained handles;
validation on the supported installed signed driver remains outstanding.

Physical devices are the primary rows in the profile tree. Expand one only when interface-level control is needed. The
*Gaming devices only* filter is enabled by default, disconnected devices are hidden by default, and selections excluded by
either filter are preserved. Executable and device labels include path, serial, or instance details when names would
otherwise be ambiguous.

## Package integration

The separately installed upstream driver package exposes the following registry keys; these are not companion MSI registration keys.
*"HKCR\Installer\Dependencies\NSS.Drivers.HidHide.x64\Version"* signals the availability of HidHide and its version.
*"HKCR\SOFTWARE\Nefarius Software Solutions e.U.\Nefarius Software Solutions e.U. HidHide\Path"* tells its location.

Third-party software deployment may benefit from the *HidHide Command Line Interface (CLI)* while deploying software.
Please be conservative while altering a clients' configuration and only extend the configuration with new features offered.
Don't assume exclusive ownership of the configuration settings as a recovery typically requires manual actions by the user.

## Bugs & Features

~~Found a bug and want it fixed? Feel free to open a detailed issue on the [GitHub issue tracker](../../issues)!~~

There is currently no capacity for any major works on HidHide, if you wish to see this change, consider contributing.

Contact us [through Discord](https://discord.nefarius.at/)!

---

The separately installed HidHide driver provides both logging and tracing. Logging can be found the *Event Viewer* under *Windows Logs* and *System*.
Tracing can be found under *Applications and Services Logs* and *Nefarius* after enabling *Show Analytic and Debug Logs*.
Extended tracing is available but switched off per default for performance reasons. Tracing is controlled using the *wevtutil* utility
which is an integral part of the operating system. To enable extended tracing, open a command shell, and enter the following;

```cmd
wevtutil set-log Nefarius-Drivers-HidHide/Diagnostic /e:false
wevtutil set-log Nefarius-Drivers-HidHide/Diagnostic /k:5
wevtutil set-log Nefarius-Drivers-HidHide/Diagnostic /e:true
```

Tracing adjustments remain in affect after a reboot. Restore tracing to its default level using the above sequence with /k:1 instead.
Tracing to the debug console is enabled with /k:3 and /k:7 respectively.

## Questions & Support

Please respect that the GitHub issue tracker isn't a help desk. [Look at the community support resources](https://docs.nefarius.at/Community-Support/).

## Donations

> From creator Eric

Creating a utility like this requires time and dedication. Should you like to express your gratitude, consider a pledge
for a game I'm rather fond of; the biggest crowd funded game currently in development *Star Citizen*. Be sure to apply a
referral code at account creation as it gives a bit more in-game currency and can't be applied later on. My referral code
is *STAR-K6S5-KPY7* should you seek one. Have fun and see you in the verse!

> From maintainer Nefarius

You can find all my donation options [over here](https://docs.nefarius.at/Donations/)!
