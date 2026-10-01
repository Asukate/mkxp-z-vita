# Vita SDL shim

SDL2 API-subset implementation for PS Vita homebrew, built as `libSDL2.a`.
This source snapshot contains the API subset used by HardRPG. Generated
build directories are excluded.

The active CMake target builds `src/SDL_vita.c` and `src/SDL_vita_stubs.c`.

## Provenance (audited 2026-09-24)

Compared file-by-file against upstream SDL `release-2.30.0`
(`https://github.com/libsdl-org/SDL`), upstream stb_image v2.30
(`https://github.com/nothings/stb`), and the Khronos registries:

- `include/SDL2/SDL*.h` (34 files): **derived from upstream SDL 2.30.0**.
  Trimmed to the API subset mkxp-z needs, Doxygen markup removed, enums
  named. Each file carries a derivation note; the SDL zlib license is
  `LICENSE-SDL.txt` (verbatim upstream `LICENSE.txt`).
- `src/SDL_vita.c`, `src/SDL_vita_stubs.c`: **locally authored** for this
  project (GPL-3.0, see `../../../LICENSES/GPL-3.0.txt`). Zero shared content with
  upstream SDL's Vita video/audio/thread/joystick drivers; implementation
  is mkxp-z-specific (nested graphics lock, SDL_QueueAudio bridge,
  TTF_Font layout coupled to mkxp-z).
- `src/stb_image.h`: **byte-identical** to upstream v2.30 (public domain).
- `include/SDL2/SDL_image.h`, `SDL_sound.h`, `SDL_ttf.h`, `alext.h`:
  **minimal local declarations** (GPL-3.0) against those libraries' APIs.
- `include/KHR/`, `include/GLES2/`: **minimal local headers** (GPL-3.0).
  No Khronos copyright text is copied; only API typedefs/constexprs are
  declared. Note `gl2.h` and `gl2ext.h` are currently the same declaration
  set under one include guard.
- `CMakeLists.txt`: local build input (GPL-3.0).

## JPEG decoding on Vita

The JPEG-only stb_image implementation uses `STBI_NO_THREAD_LOCALS` on Vita.
The shim does not use stb's thread-local flip setters or failure-reason API;
the default global flip setting stays unchanged. This avoids the emulated-TLS
mutex path that faulted during JPEG loading on the physical device. Keep this
configuration in the shared decoder, not in individual game scripts.

The 2026-09-06 test produced identical host JPEG pixels with and without TLS,
and the Vita decoder object no longer referenced `__emutls_get_address`.
To the Moon progressed past Begin into its opening scenes for over 100 seconds
without a new dump, with pressure GC disabled. Rendering corruption and later
GPU-pool pressure remain separate unresolved issues; this is not full-game or
cross-game compatibility acceptance.

Set `VITASDK` before configuring:

```bash
VITASDK=/path/to/vitasdk cmake -S . -B build-vita \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vita.toolchain.cmake
```

## Compatibility constraints

- The Vita has one display, 960x544 at 60 Hz. Display-mode APIs expose
  that display using the shim's `SDL_PIXELFORMAT_XRGB8888` format.
- `SDL_GetPrefPath(org, app)` returns `ux0:/data/` for compatibility with
  existing RGSS save-path handling. Changing this return value caused
  Black Souls 2 to exit before its title screen in physical Vita tests.
  Future save-path changes need separate compatibility testing.
