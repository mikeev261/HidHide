# Unified upgrade protocol

Implemented protocol: `HidHide.Upgrade.v1`, first compatible version 2.1.0.
Native two-version upgrade and rollback testing remains required before release.

The incoming bootstrapper requires every detected related bundle to be a cached,
per-machine upgrade relation advertising this exact Bundle Tag. The elevated
controller independently rejects earlier development versions and requires the
healthy, pinned driver. Version 2.0 previews require their normal uninstall path;
the incoming setup cannot change an already cached preview bootstrapper.
Same-version bundles with a different Burn identity are refused before handoff:
use the installed original setup for repair. Rebuilding the same version does
not silently create a second visible registration. Private MSI ProductCodes are
deterministic SHA-256-derived GUIDs from the stable MSI upgrade namespace and the
three-field MSI version; a new version receives a different ProductCode.

Every new protected setup record captures the exact private MSI ProductCode and
its embedded SHA-256. Before upgrade authorization, the controller finds the
completed protected record for the installed version, verifies that original
compressed MSI, and copies it into the new protected cache as `PriorUnified.msi`.
The new journal records the old ProductCode, version, hash and source transaction.
The local Windows Installer database must also remain readable. A stripped
`LocalPackage` database alone is never treated as a complete recovery payload.
All subsequent transaction cache checks reverify the retained original MSI.
Missing source or changed product ownership blocks before old MSI removal.

The private MSI uses `RemoveExistingProducts` after `InstallInitialize`, before
new component installation. This places old application removal inside Windows
Installer rollback. The old driver's uninstall action remains conditioned on
`NOT UPGRADINGPRODUCTCODE`, preserving the same signed driver. The new driver
transaction is Upgrade, so healthy same-payload state causes no reinstallation.

WiX 5 schedules upgrade-related bundle execution after the package chain. The
superseded bootstrapper handles `RelationType.Upgrade` without creating another
controller or entering the active maintenance transaction. On uninstall it first
requires exactly its one owned package to be Absent or Obsolete. WiX marks an
absent ProductCode Obsolete when the newer related MSI is detected; Superseded
still has an installed ProductCode and is rejected. The bootstrapper forces
every package request to None, prevents compatible-MSI removal, and suppresses
other related-bundle execution. Burn then removes its own registration and cache.
No uninstall registry entry is manually deleted. The path never clears a marker,
touches driver state, runs a GUI, or changes initiating-user startup preferences.

If Burn requests restoration of an older registration, the same restricted path
accepts Install only if that old MSI is already Present, as restored by MSI
rollback. It does not install an absent old MSI outside its original transaction.
Unknown states fail with the maintenance journal retained.

The unit callback suite exercises all WiX package states for uninstall and
registration restoration, unknown packages, compatible MSI removal and related
bundle requests. These checks do not prove actual Burn registration behavior.
Integration evidence must show 2.1 to a later version with one final Installed
Apps entry, old MSI absent/new MSI present, unchanged signed driver and baseline,
and a failed new-MSI transaction restoring the old applications. Re-run resume,
repair and uninstall against the upgraded installation as well.

Source references reviewed for this implementation:

- [WiX 5.0.2 plan.cpp](https://raw.githubusercontent.com/wixtoolset/wix/v5.0.2/src/burn/engine/plan.cpp), `PlanRelatedBundlesComplete`.
- [WiX 5.0.2 msiengine.cpp](https://raw.githubusercontent.com/wixtoolset/wix/v5.0.2/src/burn/engine/msiengine.cpp), `MsiEngineDetectPackage` and `MsiEnginePlanCalculatePackage`.
- Installed WiX BootstrapperApplicationApi 5.0.2 XML API reference, and WixSharp 2.13.0 `MajorUpgrade` / `UpgradeSchedule` documentation.
