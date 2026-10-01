/* HardRPG - minimal local SDL_ttf API declarations
 * Copyright (C) 2026 Asukate
 *
 * This file is part of HardRPG, free software under the GNU
 * General Public License v3. See LICENSES/GPL-3.0.txt at the repository root. */
#ifndef SDL_ttf_h_
#define SDL_ttf_h_
#include "SDL_stdinc.h"
#include "SDL_surface.h"
#include "SDL_rwops.h"
#include "SDL_error.h"
typedef struct TTF_Font TTF_Font;
/* TTF style flags */
#define TTF_STYLE_NORMAL       0x00
#define TTF_STYLE_BOLD         0x01
#define TTF_STYLE_ITALIC       0x02
#define TTF_STYLE_UNDERLINE    0x04
#define TTF_STYLE_STRIKETHROUGH 0x08

#ifdef __cplusplus
extern "C" {
#endif
TTF_Font *TTF_OpenFont(const char *file, int ptsize);
TTF_Font *TTF_OpenFontRW(SDL_RWops *src, int freesrc, int ptsize);
int TTF_Init(void);
void TTF_Quit(void);
int TTF_FontHeight(TTF_Font *font);
int TTF_GetFontStyle(TTF_Font *font);
void TTF_SetFontStyle(TTF_Font *font, int style);
int TTF_SizeUTF8(TTF_Font *font, const char *text, int *w, int *h);
void TTF_CloseFont(TTF_Font *font);
SDL_Surface *TTF_RenderUTF8_Blended(TTF_Font *font, const char *text, SDL_Color fg);
void TTF_SetFontKerning(TTF_Font *font, int allowed);
int TTF_GetFontOutline(TTF_Font *font);
void TTF_SetFontOutline(TTF_Font *font, int outline);
const char *TTF_FontFaceFamilyName(TTF_Font *font);
const char *TTF_FontFaceStyleName(TTF_Font *font);
int TTF_SetFontSize(TTF_Font *font, int ptsize);
void TTF_SetFontHinting(TTF_Font *font, int hinting);
int TTF_GlyphMetrics(TTF_Font *font, Uint16 ch, int *minx, int *maxx, int *miny, int *maxy, int *advance);
int TTF_MeasureUTF8(TTF_Font *font, const char *text, int measure_width, int *extent, int *count);
SDL_Surface *TTF_RenderUTF8_Solid(TTF_Font *font, const char *text, SDL_Color fg);
#ifdef __cplusplus
}
#endif
#endif
