/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_RWOPS_H
#define SDL_RWOPS_H

#include "SDL_stdinc.h"

/* SDL_RWops also needs data1/data2/... members like real SDL2 */
#ifndef SDL_RWOPS_UNKNOWN
#define SDL_RWOPS_UNKNOWN 0
#endif

typedef struct SDL_RWops {
    Sint64 (*size)(struct SDL_RWops *context);
    Sint64 (*seek)(struct SDL_RWops *context, Sint64 offset, int whence);
    size_t (*read)(struct SDL_RWops *context, void *ptr, size_t size, size_t maxnum);
    size_t (*write)(struct SDL_RWops *context, const void *ptr, size_t size, size_t num);
    int (*close)(struct SDL_RWops *context);
    Uint32 type;
    union {
        struct {
            int autoclose;
            FILE *fp;
        } stdio;
        struct {
            int autoclose;
            FILE *fp;
            void *data1;
            void *data2;
        } unknown;
    } hidden;
} SDL_RWops;

#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2

#ifdef __cplusplus
extern "C" {
#endif

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode);
SDL_RWops *SDL_RWFromFP(FILE *fp, SDL_bool autoclose);
SDL_RWops *SDL_RWFromMem(void *mem, int size);
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);
SDL_RWops *SDL_AllocRW(void);
void SDL_FreeRW(SDL_RWops *area);
Sint64 SDL_RWsize(SDL_RWops *context);
Sint64 SDL_RWseek(SDL_RWops *context, Sint64 offset, int whence);
size_t SDL_RWread(SDL_RWops *context, void *ptr, size_t size, size_t maxnum);
size_t SDL_RWwrite(SDL_RWops *context, const void *ptr, size_t size, size_t num);
Sint64 SDL_RWtell(SDL_RWops *context);
int SDL_RWclose(SDL_RWops *context);

Uint16 SDL_ReadLE16(SDL_RWops *src);
Uint32 SDL_ReadLE32(SDL_RWops *src);
Uint64 SDL_ReadLE64(SDL_RWops *src);
Sint16 SDL_ReadBE16(SDL_RWops *src);
Sint32 SDL_ReadBE32(SDL_RWops *src);

#ifdef __cplusplus
}
#endif

#endif /* SDL_RWOPS_H */
