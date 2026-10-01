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
int IMG_isGIF(SDL_RWops *src);
int IMG_SaveJPG(SDL_Surface *surface, const char *file, int quality);
int IMG_SavePNG(SDL_Surface *surface, const char *file);
const char *IMG_GetError(void);

#ifdef __cplusplus
}
#endif
#endif
