/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_CLIPBOARD_H_
#define SDL_CLIPBOARD_H_
#include "SDL_stdinc.h"
#ifdef __cplusplus
extern "C" {
#endif
int SDL_SetClipboardText(const char *text);
const char *SDL_GetClipboardText(void);
SDL_bool SDL_HasClipboardText(void);
#ifdef __cplusplus
}
#endif
#endif
