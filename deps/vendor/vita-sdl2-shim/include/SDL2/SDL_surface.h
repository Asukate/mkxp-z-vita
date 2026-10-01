/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_surface_h_
#define SDL_surface_h_
#include "SDL_stdinc.h"
#include "SDL_rect.h"
#include "SDL_rwops.h"
#include "SDL_pixels.h"
#include "SDL_blendmode.h"

typedef struct SDL_Surface {
    SDL_PixelFormat *format;
    int w, h;
    int pitch;
    void *pixels;
    void *userdata;
    int locked;
    void *lock_data;
    SDL_Rect clip_rect;
    Uint32 flags;
} SDL_Surface;

#define SDL_SWSURFACE 0
#define SDL_PREALLOC 0x00000001
#define SDL_RLEACCEL 0x00000002
#define SDL_DONTFREE 0x00000004

#ifdef __cplusplus
extern "C" {
#endif
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color);
int SDL_FillRects(SDL_Surface *dst, const SDL_Rect *rects, int count, Uint32 color);
int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
void SDL_LockSurface(SDL_Surface *s);
void SDL_UnlockSurface(SDL_Surface *s);
int SDL_MUSTLOCK(SDL_Surface *s);
Uint32 SDL_MapRGB(const SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b);
Uint32 SDL_MapRGBA(const SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void SDL_GetRGB(Uint32 pixel, const SDL_PixelFormat *fmt, Uint8 *r, Uint8 *g, Uint8 *b);
int SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha);
int SDL_GetSurfaceAlphaMod(SDL_Surface *surface, Uint8 *alpha);
int SDL_SetSurfaceBlendMode(SDL_Surface *surface, int blendMode);
int SDL_GetSurfaceBlendMode(SDL_Surface *surface, int *blendMode);
int SDL_PixelFormatEnumToMasks(Uint32 format, int *bpp, Uint32 *Rmask, Uint32 *Gmask, Uint32 *Bmask, Uint32 *Amask);
int SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_SoftStretchLinear(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_LowerBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_LowerBlitScaled(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_SaveBMP(SDL_Surface *surface, const char *file);
SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *surface, Uint32 pixel_format, Uint32 flags);
SDL_PixelFormat *SDL_AllocFormat(Uint32 pixel_format);
void SDL_FreeFormat(SDL_PixelFormat *format);
#ifdef __cplusplus
}
#endif
#endif
