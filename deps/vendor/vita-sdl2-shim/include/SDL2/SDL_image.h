/* HardRPG - minimal local SDL_image API declarations
 * Copyright (C) 2026 Asukate
 *
 * This file is part of HardRPG, free software under the GNU
 * General Public License v3. See LICENSES/GPL-3.0.txt at the repository root. */
#ifndef SDL_IMAGE_H_
#define SDL_IMAGE_H_
#include "SDL_stdinc.h"
#include "SDL_rwops.h"
#include "SDL_surface.h"
#ifdef __cplusplus
extern "C" {
#endif

#define IMG_INIT_JPG 1
#define IMG_INIT_PNG 2

int IMG_Init(int flags);
void IMG_Quit(void);
SDL_Surface *IMG_Load(const char *file);
SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc);
SDL_Surface *IMG_LoadTyped_RW(SDL_RWops *src, int freesrc, const char *type);
/* VITA_BITMAP opts into sparse storage if full PNG pixel allocation fails.
 * The returned surface contains physical pixels; this metadata preserves its
 * logical image. Ordinary SDL_image requests always return full surfaces. */
typedef struct SDL_VitaImageCrop {
    int x, y, width, height, logicalWidth, logicalHeight;
    Uint32 background;
} SDL_VitaImageCrop;
int SDL_VitaGetImageCrop(const SDL_Surface *surface, SDL_VitaImageCrop *crop);
/* When neither the full image nor its crop fits in one allocation, VITA_BITMAP
 * PNGs come back as horizontal bands of separate small allocations. Band i
 * holds rows y..y+height (one extra row above and below its own rows, for
 * filtering) and columns x..x+width; it shows rows ownTop..ownBottom. Rows of
 * no band are the background. The surface itself is a 1x1 placeholder. */
typedef struct SDL_VitaImageBand {
    int x, y, width, height, ownTop, ownBottom;
    void *pixels; /* width * 4 bytes per row; NULL once released */
    int fromPool;
} SDL_VitaImageBand;
typedef struct SDL_VitaImageBands {
    int logicalWidth, logicalHeight;
    Uint32 background;
    int count;
    SDL_VitaImageBand band[1];
} SDL_VitaImageBands;
const SDL_VitaImageBands *SDL_VitaGetImageBands(const SDL_Surface *surface);
/* Frees one band's pixels early (after its upload). */
void SDL_VitaReleaseImageBand(SDL_Surface *surface, int index);
int IMG_isGIF(SDL_RWops *src);
int IMG_SaveJPG(SDL_Surface *surface, const char *file, int quality);
int IMG_SavePNG(SDL_Surface *surface, const char *file);
const char *IMG_GetError(void);

#ifdef __cplusplus
}
#endif
#endif
