/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_messagebox_h_
#define SDL_messagebox_h_
#include "SDL_stdinc.h"

/* Message box flags */
#define SDL_MESSAGEBOX_ERROR       0x00000010
#define SDL_MESSAGEBOX_WARNING     0x00000020
#define SDL_MESSAGEBOX_INFORMATION 0x00000040
#define SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT 0x00000080
#define SDL_MESSAGEBOX_BUTTONS_RIGHT_TO_LEFT 0x00000100

typedef struct { Uint32 flags; } SDL_MessageBoxData;
#ifdef __cplusplus
extern "C" {
#endif
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, void *window);
#ifdef __cplusplus
}
#endif
#endif
