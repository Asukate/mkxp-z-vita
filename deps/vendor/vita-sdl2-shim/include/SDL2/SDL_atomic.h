/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_atomic_h_
#define SDL_atomic_h_
#include "SDL_stdinc.h"
typedef struct { int value; } SDL_atomic_t;
#define SDL_AtomicIncRef(a) SDL_AtomicAdd(a, 1)
#define SDL_AtomicDecRef(a) SDL_AtomicAdd(a, -1)
#ifdef __cplusplus
extern "C" {
#endif
int SDL_AtomicTryLock(SDL_atomic_t *lock);
void SDL_AtomicLock(SDL_atomic_t *lock);
void SDL_AtomicUnlock(SDL_atomic_t *lock);
int SDL_AtomicGet(SDL_atomic_t *a);
void SDL_AtomicSet(SDL_atomic_t *a, int v);
int SDL_AtomicAdd(SDL_atomic_t *a, int v);
int SDL_AtomicCAS(SDL_atomic_t *a, int oldval, int newval);
#ifdef __cplusplus
}
#endif
#endif
