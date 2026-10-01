/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_VERSION_H
#define SDL_VERSION_H

#include "SDL_stdinc.h"

typedef struct SDL_version {
    Uint8 major;
    Uint8 minor;
    Uint8 patch;
} SDL_version;

#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 30
#define SDL_PATCHLEVEL    0

#define SDL_VERSION(x) \
{ (x)->major = SDL_MAJOR_VERSION; \
  (x)->minor = SDL_MINOR_VERSION; \
  (x)->patch = SDL_PATCHLEVEL; }

#ifdef __cplusplus
extern "C" {
#endif

void SDL_GetVersion(SDL_version *ver);
const char *SDL_GetRevision(void);
int SDL_GetRevisionNumber(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_VERSION_H */
