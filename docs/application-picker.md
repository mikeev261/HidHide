# Creating an application profile

Choose **New profile**, then **Browse for EXE** or **Choose running application**.
The running list includes window and background applications in the current Windows
session. Search by suggested name, filename, or full path; **Refresh** takes a new
snapshot. Multiple instances of the same normalized executable path share one row.
The full path distinguishes launchers and games with the same filename. Unreadable
identities are counted as unavailable and never inferred from a process name.

After choosing an executable, review its icon, full path, and editable suggested
name. **Create draft** opens the existing detached draft; only **Apply changes**
saves it. Changing the chosen executable preserves a name you have typed. Global
profiles retain a name-only creation flow. No profile schema or stored fields change.

Choosing a running application cannot fix controller handles it has already opened.
After saving the profile, close the application and use **Launch with profile** to
apply hiding before it starts. Feeder exemptions and the launch contract are unchanged.

## Discovery boundary

`HidHideClient.exe --application-discovery` handles one piped JSON request and exits
before the editor bridge, OLE, coordinator ownership, startup integration or driver
access. It rejects elevated tokens. Electron invokes this fixed helper asynchronously
through narrow authenticated renderer IPC. Discovery never joins the serialized
configuration request queue or the coordinator's matching scan.

The helper reads process paths using limited-information access and binds selections
to PID, creation time and exact normalized path. It validates the selection both when
chosen and immediately before creating a draft. Exited/reused/inaccessible processes
require a refresh; missing browsed files require another selection. This validates a
point-in-time selection, not a guarantee that the application remains running.

Names prefer meaningful ProductName, then FileDescription, reading the user's UI
language first among declared version-resource translations. Generic engine/helper
metadata and exact generic Windows product branding are ignored; meaningful Microsoft
application names remain eligible. Exact `DefaultProduct` and `Default Product`
placeholders are ignored after whitespace/underscore normalization; longer titles
containing those words remain eligible. TODO placeholders require an exact `TODO` or
`TODO:` prefix, so names such as Todoist remain eligible. Only recognized `bin` or
`Binaries[/Win64|Win32|x64|x86]` layouts permit parent-title inference; common
installation folders are rejected. Otherwise,
the cleaned filename stem is used. No directory scan or launcher/Steam database is
consulted. Unicode names are bounded to 256 UTF-16 units without splitting a pair.

Limits: 16,384 process snapshot entries, 512 distinct executable rows, one MiB per
version resource, 64 translations, and two MiB per response. Window apps get first
priority, including visible windows with an empty caption. After eight seconds further
list metadata uses filename/directory fallback; the Electron helper deadline is
15 seconds. Partial/unavailable results are labelled.
New requests, dialog exit, and application exit cancel helpers. Only describe results
are cached (128 entries, 30 seconds); validation always queries again. Icons use the
existing independent bounded icon loader. Slow or inaccessible files may require
retrying; no privilege elevation or guessed process path is offered.

## Regression checks

- `build.ps1 Ci --configuration Release --platform x64` with the explicit compiler
  and verified driver payload described in `BUILD_AND_RELEASE.md`.
- `npm test` and `npm run test:applications` in `Editor` (the latter requires a built
  Release native client and a desktop session). Pure native tests cover grouping,
  inaccessible/exited/reused identity, name priority, generic directories, Unicode
  limits and bounded metadata work. A harmless off-screen native window checks that
  untitled visible windows count and hidden windows do not. JavaScript tests cover
  IPC runner limits, cancellation, cache/validation separation and helper failures.
- The Electron picker fixture covers staged creation/Apply/discard, manual names,
  search, grouped/background rows and stale replies across type changes and closing.
  The native discovery fixture starts harmless Node sleepers and exercises the actual
  helper through Electron/preload into a draft, using only a fixture policy engine.
  Screenshots are saved in `artifacts/application-picker`.
- Existing `Editor/tests/acceptance.mjs` and `Editor/tests/launch-order.mjs` retain
  editor and suspended-child ordering regression coverage.

These tests do not install the MSI, change the host driver/profile repository, or
establish physical-controller/game acceptance.
