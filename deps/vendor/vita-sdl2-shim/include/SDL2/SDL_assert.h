/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_ASSERT_H
#define SDL_ASSERT_H

#include "SDL_stdinc.h"

#define SDL_ASSERT_LEVEL 1

typedef enum {
    SDL_ASSERTION_RETRY,
    SDL_ASSERTION_BREAK,
    SDL_ASSERTION_ABORT,
    SDL_ASSERTION_IGNORE,
    SDL_ASSERTION_ALWAYS_IGNORE
} SDL_AssertState;

typedef struct SDL_AssertData {
    int always_ignore;
    unsigned int trigger_count;
    const char *condition;
    const char *filename;
    int linenum;
    const char *function;
    const struct SDL_AssertData *next;
} SDL_AssertData;

#ifdef __cplusplus
extern "C" {
#endif

SDL_AssertState SDL_ReportAssertion(SDL_AssertData *data, const char *func, const char *file, int line);

#if SDL_ASSERT_LEVEL > 0
#define SDL_assert(condition) \
    do { \
        if (!(condition)) { \
            static SDL_AssertData __assert_data = { 0, 0, #condition, __FILE__, __LINE__, __func__, NULL }; \
            SDL_ReportAssertion(&__assert_data, __func__, __FILE__, __LINE__); \
        } \
    } while(0)
#else
#define SDL_assert(condition)
#endif

#define SDL_assert_release(condition) SDL_assert(condition)

#ifdef __cplusplus
}
#endif

#endif /* SDL_ASSERT_H */
