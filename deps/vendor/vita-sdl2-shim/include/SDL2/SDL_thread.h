/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_THREAD_H
#define SDL_THREAD_H

#include "SDL_stdinc.h"

typedef unsigned long SDL_threadID;
typedef int (*SDL_ThreadFunction)(void *data);

typedef enum {
    SDL_THREAD_PRIORITY_LOW,
    SDL_THREAD_PRIORITY_NORMAL,
    SDL_THREAD_PRIORITY_HIGH,
    SDL_THREAD_PRIORITY_TIME_CRITICAL
} SDL_ThreadPriority;

typedef struct SDL_Thread SDL_Thread;

#ifdef __cplusplus
extern "C" {
#endif

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
SDL_Thread *SDL_CreateThreadWithStackSize(SDL_ThreadFunction fn, const char *name, const size_t stacksize, void *data);
const char *SDL_GetThreadName(SDL_Thread *thread);
SDL_threadID SDL_GetThreadID(SDL_Thread *thread);
SDL_threadID SDL_ThreadID(void);
int SDL_SetThreadPriority(SDL_ThreadPriority priority);
int SDL_WaitThread(SDL_Thread *thread, int *status);
void SDL_DetachThread(SDL_Thread *thread);

#ifdef __cplusplus
}
#endif

#endif /* SDL_THREAD_H */
