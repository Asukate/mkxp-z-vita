/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_POWER_H
#define SDL_POWER_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SDL_PowerState {
    SDL_POWERSTATE_UNKNOWN,
    SDL_POWERSTATE_ON_BATTERY,
    SDL_POWERSTATE_NO_BATTERY,
    SDL_POWERSTATE_CHARGING,
    SDL_POWERSTATE_CHARGED
} SDL_PowerState;

SDL_PowerState SDL_GetPowerInfo(int *seconds, int *percent);

#ifdef __cplusplus
}
#endif

#endif
