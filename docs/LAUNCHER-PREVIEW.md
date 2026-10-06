# Iterating the launcher on a PC

The native launcher is the default. Its production C++ model, UI and FreeType
canvas run on a PC through the [native preview](NATIVE-LAUNCHER.md#desktop-iteration).
The Ruby preview below is available for the alternative RGSS picker.

## Alternative RGSS preview

The desktop preview executes the production `launcher/library.rb` and
`launcher/picker.rb`, with a small SDL adapter for their RGSS drawing/input
calls. It compiles the exact native PhysicsFS ZIP bridge as a host Ruby
extension. It is useful for layout, About text, navigation, game detection,
ZIP preparation, and handoff checks. It does not emulate the Vita renderer,
embedded Ruby, controller, game performance, or Vita file I/O.

Linux prerequisites: Ruby and its development headers, a C++ compiler,
pkg-config, PhysicsFS development libraries, SDL2, and SDL2_ttf.

```bash
ruby scripts/preview-launcher.rb --work /absolute/path/to/preview --demo
```

The demo creates metadata-only game fixtures in its own preview directory;
it contains no game assets. Arrow keys navigate, Enter/Space select,
Escape/Backspace go back, T refreshes, S opens/closes the folder browser,
and Q scans its current folder (Vita Square). To browse your own copied test
library, use `--data-root /absolute/path/to/test/hardrpg` instead of `--demo`.
That directory gets the same `games/`, `rtp/`, `config/`, and `cache/` layout.
Selecting a game writes the launch handoff and closes the desktop preview;
it does not run a Vita executable.

Capture an actual launcher render, including scripted navigation:

```bash
xvfb-run -a env SDL_VIDEODRIVER=x11 ruby scripts/preview-launcher.rb \
  --work /absolute/path/to/preview --demo --keys down,down \
  --screenshot /absolute/path/to/about.png
```

Supported scripted keys: `up`, `down`, `left`, `right`, `cross`, `circle`,
`triangle`, `start`, and `square`. Add `--browse-root /path/to/collections`
to expose another host folder in the preview's browser; otherwise it stays
inside the preview workspace. Adding games stores references without copying
their files. The command runs without a visible desktop window under
Xvfb and saves the rendered 960×544 frame. This creates UI screenshots,
not branding artwork or packaged image assets.

Run the browser/native archive integration checks:

```bash
ruby tests/launcher-library.rb /absolute/path/to/test-output
```

The tests use temporary synthetic metadata/ZIP payloads and remove their
test library afterward. They exercise the production PhysicsFS backend,
nested paths, game versions, JSON handoff, persistent saves, changed ZIPs,
cancellation, traversal rejection, and corrupt archives.

## Native launcher preview

The default launcher has a preview using the production C++ menu and
FreeType canvas. It does not use the RGSS adapter. See
[native launcher details](NATIVE-LAUNCHER.md) for controls,
build options, prerequisites and hardware acceptance checks.

```bash
python3 scripts/preview-native-launcher.py --work /path/to/native-preview --demo
```

## Vita3K

Build the separate `vita3k` profile using [BUILDING.md](BUILDING.md). It uses
vitaGL's emulator support flag. Run it in a separate Vita3K preference/data
directory with your already installed firmware/modules, keeping emulator
games and saves isolated. The desktop preview is the fast UI iteration path;
Vita3K checks the packaged ARM program and embedded Ruby. An emulator result
does not establish physical-Vita rendering, audio, timing, or save acceptance.
