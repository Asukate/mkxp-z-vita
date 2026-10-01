# Third-party notices (vita-sdl2-shim)

Do not flatten into one license statement. Each part keeps its own terms.

- `include/SDL2/SDL*.h` (34 headers): derived from SDL `release-2.30.0`
  (https://github.com/libsdl-org/SDL), trimmed to the mkxp-z subset.
  Zlib license; verbatim upstream text in `LICENSE-SDL.txt`.
- `src/stb_image.h`: upstream stb v2.30, public domain / MIT (see header).
- `src/SDL_vita.c`, `src/SDL_vita_stubs.c`, `CMakeLists.txt`, `include/KHR/*`,
  `include/GLES2/*`, `include/SDL2/SDL_image.h`, `SDL_sound.h`, `SDL_ttf.h`,
  `alext.h`: locally authored for this project; source headers claim GPL-3.0.
  Full GPL text is in the repository root at `LICENSES/GPL-3.0.txt`.
- Upstream mkxp-z consumer is GPL-2.0-or-later (not GPL-2.0-only).
