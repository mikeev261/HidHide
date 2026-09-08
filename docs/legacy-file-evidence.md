# Legacy file recovery evidence

`Installer.Controller/LegacyFileEvidence.cs` uses fixed ownership maps from the
actual supported MSI File, Component, Directory and Shortcut tables. It reads
component key paths with [MsiGetComponentPathExW](https://learn.microsoft.com/en-us/windows/win32/api/msi/nf-msi-msigetcomponentpathexw)
in the per-machine context, avoiding repair-triggering inventory APIs.

The upstream 1.5.230 MSI hash is
`974F2F56BB9D87E760FFDC757BBAA1AD94CEFB57431D73B1E5E209F262B8C9F7`.
Its SET_APPDIR action resolves to `%ProgramFiles%\Nefarius Software Solutions\HidHide`.
It owns 13 files: the manifest in that root; applications, PDB, scripts, nefcon,
watchdog and updater under `x64`; and the signed driver/license under `x64\HidHide`.
The legacy recovery source includes those legacy applications. They are not
included in the successful unified product's installed application layout.

Upstream shortcuts are `HidHide Configuration Client.lnk` in CommonDesktop and
CommonStartMenu, targeting `x64\HidHideClient.exe`. Companion versions 1 and 99
own two executables in `%ProgramFiles%\HidHide App Profiles`, and one shortcut in
CommonPrograms\HidHide App Profiles. Their shared component GUIDs are resolved
with the exact product identity so version 99 cannot masquerade as version 1.

Before removal, every mapped file must match its verified recovery-source hash
and registered default component path. Missing, modified or customized legacy
files/paths are preserved and require a separate compatibility decision. The
fixed recovery source cannot prove restoration of arbitrary changed files.
Reparse points and oversized inputs are rejected. Journal keys never supply
filesystem paths or commands.

Shortcut evidence records canonical target, empty arguments and working directory,
plus original presence or absence. Regenerating a shortcut can change its bytes,
so file hashes alone are not used to verify shortcut restoration. Customized
shortcut targets stop before migration. Restoration must preserve recorded
shortcut preferences before the maintenance marker can be cleared.

The exact companion recovery MSI hashes inspected here are:

- 1.0.0.0: `A9877ED39F5D36998302FBCB2BE94EC8F5C5C59C5C2990BC41B25A6A772B9431E6`
- 99.0.0.0: `EBC9EE898608C3265E59960A9F08EA13DDF4E327D89F33936F6E7ABBC43DE4B4`

Read-only tables and cabinet extraction evidence are retained under
`artifacts/unified-evidence/legacy-layout-*`, `legacy1-files`, and `legacy99-files`.
These establish source ownership maps; they do not prove a native restore has
completed. The disposable VM migration/recovery matrix remains required.
