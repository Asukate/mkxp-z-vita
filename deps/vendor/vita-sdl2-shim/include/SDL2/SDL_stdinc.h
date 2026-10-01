/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_STDINC_H
#define SDL_STDINC_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

/* strdup fallback for platforms that don't define it */
#ifndef HAVE_STRDUP
#define HAVE_STRDUP 1
char *strdup(const char *s);
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int8_t      Sint8;
typedef uint8_t     Uint8;
typedef int16_t     Sint16;
typedef uint16_t    Uint16;
typedef int32_t     Sint32;
typedef uint32_t    Uint32;
typedef int64_t     Sint64;
typedef uint64_t    Uint64;

typedef size_t SDL_bool;
#define SDL_TRUE  1
#define SDL_FALSE 0
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define SDL_PRIs64  "lld"
#define SDL_PRIu64  "llu"
#define SDL_PRIx64  "llx"
#define SDL_PRIX64  "llX"
#define SDL_PRIs32  "d"
#define SDL_PRIu32  "u"
#define SDL_PRIx32  "x"
#define SDL_PRIX32  "X"

#define SDL_MAX_SINT32  ((Sint32)0x7FFFFFFF)
#define SDL_MIN_SINT32  ((Sint32)(~0x7FFFFFFF))

#define SDL_static_cast(type, val) ((type)(val))
#define SDL_reinterpret_cast(type, val) ((type)(val))
#define SDL_const_cast(type, val) ((type)(val))

/* SDL stdlib helpers */
#define SDL_free free
#define SDL_getenv getenv
#define SDL_memset memset
#define SDL_memcpy memcpy
#define SDL_strlen strlen
#define SDL_strlcpy strlcpy
#define SDL_snprintf snprintf

#define SDL_arraysize(array) (sizeof(array)/sizeof(array[0]))

#ifdef __cplusplus
}
#endif

#endif /* SDL_STDINC_H */
