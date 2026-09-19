# Windows Installer regression probe

Manual, disposable Windows-only integration fixture for the public MSI changes.
It installs one text file and a test-only protected marker, never a HidHide driver
or profile. It is not shipped and is not run by normal CI. Use only an authorized
interactive test host; preserve MSI logs and finish removal before leaving it.

Build Release with `dotnet build tools/Installer.MsiProbe`, then invoke its net48
EXE with `build` from the repository root. The child executable path in Program.cs
currently targets the development checkout at `C:\Code\HidHide`; adjust it before
building on another checkout. The resulting MSI is under
`artifacts/installer-fix-validation/msi-probe`.

The fixture uses the production removal policy and token launcher. Install it,
then request removal with `REBOOT=ReallySuppress` and verbose logging. The first
phase should return 1604 with the fixture file and registered product retained.
Continue using the same MSI with `AFTERREBOOT=1 REBOOT=ReallySuppress`; it should
return 0 and remove its file, registration and marker. This explicitly simulated
continuation exercises MSI sequencing, not a real driver or reboot. Install the
fixture from an elevated process and remove it from an ordinary client to cover
the elevated-consent immediate-action path. Inspect TOKEN_PROBE and REMOVAL_PLAN
in the logs. Never run these probes without suppressing automatic restart.

The EXE's `failure <absolute-msi-path>` mode invokes the production error reporter
and verifies that Windows Installer delivers its diagnostic error callback.
This fails before installation and requires no driver operation.

Fixed fixture identities:

- ProductCode: `{27E26E54-51C8-407E-B931-1208622128AC}`
- UpgradeCode: `{943247B8-13A9-4A1C-AF2B-0367BDCF9913}`
- File: `%ProgramFiles%\HidHide MSI Regression Probe\fixture.txt`
- Marker: `HKLM\SOFTWARE\HidHideMsiRegressionProbe`

Prefer normal continuation for cleanup. If a probe fails before suspension,
`msiexec /x {27E26E54-51C8-407E-B931-1208622128AC} SKIPPROBE=1 REBOOT=ReallySuppress`
removes only the test MSI while skipping probe actions; inspect any remaining
marker separately. Do not delete product registration to simulate removal.
Manual continuation can leave the fixture's RunOnce command after MSI removes its
continuation data. Verify product/file/marker absence, back up that exact command,
and remove only the obsolete test value. The recorded 2026-09-18 runs and bounded
cleanup scripts are in `artifacts/installer-fix-validation`.
