/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_CPUINFO_H
#define SDL_CPUINFO_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

int SDL_GetCPUCount(void);
int SDL_GetSystemRAM(void);
int SDL_GetCPUCacheLineSize(void);
SDL_bool SDL_HasRDTSC(void);
SDL_bool SDL_HasAltiVec(void);
SDL_bool SDL_HasMMX(void);
SDL_bool SDL_Has3DNow(void);
SDL_bool SDL_HasSSE(void);
SDL_bool SDL_HasSSE2(void);
SDL_bool SDL_HasSSE3(void);
SDL_bool SDL_HasSSE41(void);
SDL_bool SDL_HasSSE42(void);
SDL_bool SDL_HasAVX(void);
SDL_bool SDL_HasAVX2(void);
SDL_bool SDL_HasAVX512F(void);
SDL_bool SDL_HasNEON(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_CPUINFO_H */
