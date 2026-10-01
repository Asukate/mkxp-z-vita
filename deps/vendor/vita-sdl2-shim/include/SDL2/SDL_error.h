/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_ERROR_H
#define SDL_ERROR_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

int SDL_SetError(const char *fmt, ...);
const char *SDL_GetError(void);
void SDL_ClearError(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_ERROR_H */
