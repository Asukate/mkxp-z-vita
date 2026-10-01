/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_MUTEX_H
#define SDL_MUTEX_H

#include "SDL_stdinc.h"

typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_semaphore SDL_sem;
typedef struct SDL_cond SDL_cond;

#ifdef __cplusplus
extern "C" {
#endif

SDL_mutex *SDL_CreateMutex(void);
void SDL_DestroyMutex(SDL_mutex *mutex);
int SDL_LockMutex(SDL_mutex *mutex);
int SDL_TryLockMutex(SDL_mutex *mutex);
int SDL_UnlockMutex(SDL_mutex *mutex);

SDL_sem *SDL_CreateSemaphore(Uint32 initial_value);
void SDL_DestroySemaphore(SDL_sem *sem);
int SDL_SemWait(SDL_sem *sem);
int SDL_SemTryWait(SDL_sem *sem);
int SDL_SemWaitTimeout(SDL_sem *sem, Uint32 ms);
int SDL_SemPost(SDL_sem *sem);
Uint32 SDL_SemValue(SDL_sem *sem);

SDL_cond *SDL_CreateCond(void);
void SDL_DestroyCond(SDL_cond *cond);
int SDL_CondSignal(SDL_cond *cond);
int SDL_CondBroadcast(SDL_cond *cond);
int SDL_CondWait(SDL_cond *cond, SDL_mutex *mutex);
int SDL_CondWaitTimeout(SDL_cond *cond, SDL_mutex *mutex, Uint32 ms);

#ifdef __cplusplus
}
#endif

#endif /* SDL_MUTEX_H */
