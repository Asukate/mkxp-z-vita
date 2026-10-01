/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_LOADSO_H_
#define SDL_LOADSO_H_
#include "SDL_stdinc.h"
#ifdef __cplusplus
extern "C" {
#endif
void *SDL_LoadObject(const char *sofile);
void *SDL_LoadFunction(void *handle, const char *name);
void SDL_UnloadObject(void *handle);
#ifdef __cplusplus
}
#endif
#endif /* SDL_LOADSO_H_ */
