/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_ENDIAN_H
#define SDL_ENDIAN_H

#include "SDL_stdinc.h"

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321

#ifndef SDL_BYTEORDER
#if defined(__ARMEB__) || defined(__BIG_ENDIAN__)
#define SDL_BYTEORDER SDL_BIG_ENDIAN
#else
#define SDL_BYTEORDER SDL_LIL_ENDIAN
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

Uint16 SDL_Swap16(Uint16 x);
Uint32 SDL_Swap32(Uint32 x);
Uint64 SDL_Swap64(Uint64 x);

#define SDL_SwapLE16(x) (x)
#define SDL_SwapLE32(x) (x)
#define SDL_SwapLE64(x) (x)
#define SDL_SwapBE16(x) SDL_Swap16(x)
#define SDL_SwapBE32(x) SDL_Swap32(x)
#define SDL_SwapBE64(x) SDL_Swap64(x)

#ifdef __cplusplus
}
#endif

#endif /* SDL_ENDIAN_H */
