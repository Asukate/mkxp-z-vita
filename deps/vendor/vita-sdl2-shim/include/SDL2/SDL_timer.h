/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_TIMER_H
#define SDL_TIMER_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

Uint32 SDL_GetTicks(void);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
void SDL_Delay(Uint32 ms);

typedef Uint32 (*SDL_TimerCallback)(Uint32 interval, void *param);
typedef int SDL_TimerID;
SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param);
SDL_bool SDL_RemoveTimer(SDL_TimerID id);

#ifdef __cplusplus
}
#endif

#endif /* SDL_TIMER_H */
