# Native HardRPG launcher

HardRPG 0.2.1 Alpha uses a C++ launcher with a FreeType text canvas and an
SDL/vitaGL presenter. Browsing does not initialize Ruby, the RGSS game renderer
or OpenAL. Games continue to use the same RGSS runtime.

The native backend is the default build. `build-vita.sh --rgss-launcher`
selects the alternative Ruby/RGSS picker. Packaging reads the selected backend
from the verified engine receipt. Both backends use Title ID `HARDRPG01` by
default and the same library, settings, game/save identities and ZIP caches.
See [BUILDING.md](BUILDING.md) for the container and packaging commands.

## Library and navigation

The menu starts on Games. Right or Cross enters the list; only the focused
list shows a game cursor. Left/Circle returns to the tabs at the root and
preserves the previous selection.

- Select opens the Vita keyboard to search the current listing by display
  name. An empty search clears it; cancelling keeps the current search.
- L/R cycles All, XP, VX and VX Ace. Active filters are labelled above the list.
- Square changes a game display name without renaming files, caches or saves.
  A blank name restores the original. Names persist in `game-names.json`.
- Triangle refreshes. Start opens the storage browser; Square scans its folder
  and adds references without moving game files.
- The scrollbar shows the visible part of the list. Drag the front touchscreen
  to scroll; tap to select and tap again to open. Tabs and settings also accept
  touch input; the storage browser uses buttons.
- Full library paths appear above the list. L+R or tapping the paths opens a
  scrollable list when the paths exceed the header space.

Folders and ZIP games show XP/VX/VX Ace engine labels. ZIP metadata is inspected
without extraction. Launching shows a Starting game panel immediately; ZIP
preparation displays progress and can be cancelled. Completed working copies
are reused and never automatically evicted. See [library and save details](../launcher/README.md).

## Settings and errors

Display settings control aspect ratio, scaling and filtering. Extra game
folders and custom RTP folders, collections or ZIPs persist in the same path
files used by the RGSS picker. RTP inspection results are cached during the
launcher session; Triangle or a changed path refreshes detection. Missing RTP
shows a message before game preparation or restart.

Settings -> Game error history lists retained errors newest first, with the
game name and UTC timestamp. Cross opens a trace. Square exports the selected
report to `ux0:/data/hardrpg/reports/error-000001.txt` with a unique number.
Retrieve it with VitaShell USB or FTP to share a bug report. A write failure is
shown explicitly. Reports include the version, backend, engine and source
build identifier plus the retained trace; game data, saves and network settings
are not collected. A native crash without a trace cannot be listed.

Hold L+R+Select for one second in a game to return to the launcher. Unsaved
progress is lost. Handled script failures also return to the menu. The splash
appears only on a fresh app launch. About includes dated release notes.

## Rendering and keyboard integration

The menu redraws on state changes and reuses its texture while idle. The panel
background, memory-backed font and glyph bitmaps are cached. The Vita keyboard
composites over a freshly drawn menu each frame and waits for presentation.
A native-only linker adapter directs the pinned vitaGL common-dialog update to
the next presented buffer. Review this adapter when changing vitaGL because it
uses that backend's internal buffer symbols.

## Desktop iteration

Linux prerequisites: a C++ compiler, pkg-config, PhysicsFS, FreeType, libpng,
SDL2 and GLESv2 development libraries. Ruby development headers are needed for
the differential tests against the alternative RGSS picker.

```bash
python3 scripts/preview-native-launcher.py --work /path/to/native-preview --demo
python3 scripts/preview-native-launcher.py --work /path/to/native-preview --demo \
  --keys down,down --screenshot /path/to/about.png
```

The metadata-only demo builds and runs the production model, UI and canvas.
Arrow keys navigate, Enter selects, Escape goes back, Space is Square, R
refreshes and Tab is Start. Use `--data-root` for a copied test library and
`--browse-root` for another host folder. Launch prepares a ZIP if needed,
writes a host handoff and closes the preview; it does not run a Vita game.
Keep preview libraries separate from original game data.

```bash
./scripts/build-native-launcher.sh /path/to/host/hardrpg-native
python3 tests/native-launcher.py /path/to/host/hardrpg-native /path/to/test-output
ruby tests/launcher-library.rb /path/to/test-output
```

Host checks and an offscreen GLES render test cover the launcher state and
presentation paths. Physical-Vita results are recorded in
[VALIDATION.md](VALIDATION.md).
