# Iterating the launcher on a PC

The launcher's production C++ model, UI and FreeType canvas run on a PC
through the desktop preview. It is the fast path for layout, navigation,
game detection, ZIP preparation and handoff checks. It does not emulate the
Vita's renderer, controller, game performance or file I/O. See
[native launcher details](NATIVE-LAUNCHER.md#desktop-iteration) for its
controls, options and prerequisites.

```bash
python3 scripts/preview-native-launcher.py --work /path/to/native-preview --demo
```

The demo creates metadata-only game fixtures in its own preview directory;
it contains no game assets. To browse your own copied test library, use
`--data-root /path/to/test/hardrpg` instead of `--demo`. Selecting a game
writes the launch handoff and closes the preview; it does not run a Vita
executable. `--keys down,cross --screenshot out.png` renders after scripted
buttons instead of opening a window.

Run every launcher test (UI and library, artwork downloader, cover cache):

```bash
./scripts/test-native-launcher.sh /path/to/test-output
```

## Vita3K

Build the separate `vita3k` profile using [BUILDING.md](BUILDING.md). It uses
vitaGL's emulator support flag. Run it in a separate Vita3K preference/data
directory with your already installed firmware/modules, keeping emulator
games and saves isolated. The desktop preview is the fast UI iteration path;
Vita3K checks the packaged ARM program and embedded Ruby. An emulator result
does not establish physical-Vita rendering, audio, timing, or save acceptance.
