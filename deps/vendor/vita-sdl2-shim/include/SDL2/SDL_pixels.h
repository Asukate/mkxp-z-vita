/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_pixels_h_
#define SDL_pixels_h_
#include "SDL_stdinc.h"

/* SDL color/format types — belong here per SDL2 spec */
typedef struct SDL_Color { Uint8 r; Uint8 g; Uint8 b; Uint8 a; } SDL_Color;

typedef struct SDL_Palette {
    int ncolors;
    Uint32 version;
    int refcount;
    struct { Uint8 r, g, b, a; } *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
    Uint32 format;
    SDL_Palette *palette;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 padding[2];
    Uint32 Rmask;
    Uint32 Gmask;
    Uint32 Bmask;
    Uint32 Amask;
    Uint8 Rloss;
    Uint8 Gloss;
    Uint8 Bloss;
    Uint8 Aloss;
    Uint8 Rshift;
    Uint8 Gshift;
    Uint8 Bshift;
    Uint8 Ashift;
    int refcount;
    struct SDL_PixelFormat *next;
} SDL_PixelFormat;

/* SDL pixel format constants */
#define SDL_PIXELFORMAT_UNKNOWN 0
#define SDL_PIXELFORMAT_INDEX1LSB 1
#define SDL_PIXELFORMAT_INDEX1MSB 2
#define SDL_PIXELFORMAT_INDEX4LSB 3
#define SDL_PIXELFORMAT_INDEX4MSB 4
#define SDL_PIXELFORMAT_INDEX8 5
#define SDL_PIXELFORMAT_RGB332 6
#define SDL_PIXELFORMAT_XRGB4444 7
#define SDL_PIXELFORMAT_RGB444 8
#define SDL_PIXELFORMAT_XBGR4444 9
#define SDL_PIXELFORMAT_BGR444 10
#define SDL_PIXELFORMAT_XRGB1555 11
#define SDL_PIXELFORMAT_RGB555 12
#define SDL_PIXELFORMAT_XBGR1555 13
#define SDL_PIXELFORMAT_BGR555 14
#define SDL_PIXELFORMAT_ARGB4444 15
#define SDL_PIXELFORMAT_RGBA4444 16
#define SDL_PIXELFORMAT_ABGR4444 17
#define SDL_PIXELFORMAT_BGRA4444 18
#define SDL_PIXELFORMAT_ARGB1555 19
#define SDL_PIXELFORMAT_RGBA5551 20
#define SDL_PIXELFORMAT_ABGR1555 21
#define SDL_PIXELFORMAT_BGRA5551 22
#define SDL_PIXELFORMAT_RGB565 23
#define SDL_PIXELFORMAT_BGR565 24
#define SDL_PIXELFORMAT_RGB24 25
#define SDL_PIXELFORMAT_BGR24 26
#define SDL_PIXELFORMAT_XRGB8888 27
#define SDL_PIXELFORMAT_RGB888 28
#define SDL_PIXELFORMAT_XBGR8888 29
#define SDL_PIXELFORMAT_BGR888 30
#define SDL_PIXELFORMAT_ARGB8888 31
#define SDL_PIXELFORMAT_RGBA8888 32
#define SDL_PIXELFORMAT_ABGR8888 33
#define SDL_PIXELFORMAT_BGRA8888 34
#define SDL_PIXELFORMAT_ARGB2101010 35
#endif
