/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_touch_h_
#define SDL_touch_h_
#include "SDL_stdinc.h"
typedef Sint64 SDL_TouchID;
typedef Sint64 SDL_FingerID;
#define SDL_TOUCH_MOUSEID ((Uint32)-1)
#ifdef __cplusplus
extern "C" {
#endif
int SDL_GetNumTouchDevices(void);
SDL_TouchID SDL_GetTouchDevice(int index);
int SDL_GetNumTouchFingers(SDL_TouchID touchID);
#ifdef __cplusplus
}
#endif
#endif
