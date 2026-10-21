# Native HardRPG launcher

HardRPG uses a C++ launcher with a FreeType text canvas and an
SDL/vitaGL presenter. Browsing does not initialize Ruby, the RGSS game renderer
or OpenAL. Games continue to use the same RGSS runtime.

It is the only launcher: the Ruby/RGSS picker of 0.2 was retired after 0.3.1.
Its library, settings, game/save identities and ZIP caches are read
unchanged, so a library set up with it keeps working. The default Title ID is
`HARDRPG01`. See [BUILDING.md](BUILDING.md) for the container and packaging
commands.

## Library and navigation

Home shows the library as a cover carousel. Left/Right move between games and
Cross plays the focused one, or opens a collection (Circle goes back out).
Square opens the game's details in place. The card after the last game adds
games.

- Up reaches the toolbar: carousel/grid view, Add games, search and filter.
  Up again reaches the top bar's About and Settings (the gear). L/R switch the
  engine tabs.
- Select opens the Vita keyboard to search by display name; Circle clears the
  search.
- Triangle opens the game's options: favourite, artwork for this game or all
  games, game settings and rename. Rename changes the display name without
  renaming files, caches or saves; a blank name restores the original. Names
  persist in `game-names.json`.
- Game settings hold controls, display, audio/MIDI, performance metrics, RTP
  locations, game information, Play and Remove from library. Circle returns to
  the same game.
- The filter panel hides engines, shows favourites or unplayed games only, and
  sorts by recent play, name or play time (choosing the same sort reverses it).
- The library scans automatically on each fresh app boot, including extra
  search folders. Start opens the storage browser; Square scans its folder
  and adds references without moving game files.
- Touch: swipe the carousel; tap a card to focus it, again for its details and
  again to play. The toolbar, top bar and hint bar are tappable. On other
  screens, tap a row to focus it and tap it again to open it.

Folders and ZIP games show XP/VX/VX Ace engine labels. ZIP metadata is inspected
without extraction. Launching shows a Starting game panel immediately; ZIP
preparation displays progress and can be cancelled. Completed working copies
are reused and never automatically evicted. See [library and save details](../launcher/README.md).

## Settings and errors

Settings contains global defaults and shared library preferences. Game settings
opened with Triangle save per-game overrides for display, audio/MIDI and metrics.
Square on an override restores inheritance from the global default. Existing
config fields are preserved. Wrapped single-game ZIPs can be configured before
launch without extracting the archive; collections must be opened first.

Display settings control aspect ratio, scaling and filtering. Extra game
folders and custom RTP folders, collections or ZIPs persist in the same path
files the 0.2 picker used. RTP inspection results are cached during the
launcher session; Triangle or a changed path refreshes detection. Missing RTP
shows a message before game preparation or restart.

Settings -> Game error history lists retained errors newest first, with the
game name and UTC timestamp. Cross opens a trace. Square exports the selected
report to `ux0:/data/hardrpg/reports/error-000001.txt` with a unique number.
Retrieve it with VitaShell USB or FTP to share a bug report. A write failure is
shown explicitly. Reports include the version, backend, engine and source
build identifier plus the retained trace; game data, saves and network settings
are not collected. A native crash without a trace cannot be listed.

Hold L+R+Select in a game to return to the launcher. Unsaved
progress is lost. Handled script failures also return to the menu. The splash
appears only on a fresh app launch. About includes dated release notes.

## Rendering and keyboard integration

The launcher is a GPU scene of image quads; text is drawn on the CPU into
cached layers only when it changes, and the memory-backed font and glyph
bitmaps are cached. The Vita keyboard composites over the scene each frame and
waits for presentation.
A native-only linker adapter directs the pinned vitaGL common-dialog update to
the next presented buffer. Review this adapter when changing vitaGL because it
uses that backend's internal buffer symbols.

## Desktop iteration

Linux prerequisites: a C++ compiler, pkg-config, PhysicsFS, FreeType, libpng,
SDL2 and GLESv2 development libraries; libjpeg-turbo, libwebp and libcurl for
artwork.

```bash
python3 scripts/preview-native-launcher.py --work /path/to/native-preview --demo
python3 scripts/preview-native-launcher.py --work /path/to/native-preview --demo \
  --keys up,up,cross --screenshot /path/to/about.png
```

The metadata-only demo builds and runs the production model, UI and canvas.
Arrow keys navigate, Enter selects, Escape goes back, Space is Square, R is
Triangle (the game's options), F is Select, Q and E are L and R, and Tab is
Start. The mouse stands in for the touch screen. Use `--data-root` for a copied test library and
`--browse-root` for another host folder. Launch prepares a ZIP if needed,
writes a host handoff and closes the preview; it does not run a Vita game.
Keep preview libraries separate from original game data.

```bash
./scripts/test-native-launcher.sh /path/to/test-output
```

The first builds the desktop host and runs the launcher's three suites: the
UI and library tests (`tests/native-launcher.py`), the artwork downloader
against canned Steam, RAWG and VNDB replies (`tests/art-fetch.cpp`, no
network), and the cover cache (`tests/art-loader.cpp`). They need the
development packages for SDL2, GLES2, PhysFS, FreeType, libpng,
libjpeg-turbo, libwebp and libcurl; CI does not run them yet.

Host checks and an offscreen GLES render test cover the launcher state and
presentation paths. Physical-Vita results are recorded in
[VALIDATION.md](VALIDATION.md).
