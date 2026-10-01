/* HardRPG - SDL_image/TTF/sound stub implementations for Vita
 * Copyright (C) 2026 Asukate
 *
 * This file is part of HardRPG, free software under the GNU
 * General Public License v3. See LICENSES/GPL-3.0.txt at the repository root. */
/**
 * Stub implementations — bare C, no header dependencies
 */
#define _GNU_SOURCE 1
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "SDL_surface.h"
#include "SDL_error.h"
#include "SDL_ttf.h"
#include "SDL_image.h"
#include "SDL_rwops.h"
#include "SDL_video.h"
#include "SDL_pixels.h"
#include "SDL_mouse.h"
#include "SDL_stdinc.h"
#include "SDL_joystick.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <png.h>

/* JPEG (and BMP/TGA fallback) decode via stb_image, vendored above.
 * Only the JPEG decoder is compiled in to keep the stub small; PNG keeps
 * its libpng fast path. */
#define STBI_ONLY_JPEG
/* The Vita toolchain implements stb's TLS through libgcc emutls. On-device
 * JPEG decoding faults in pthread_mutex_unlock on its null emutls_mutex.
 * This shim uses no thread-local flip setters or stb failure-reason API;
 * decode buffers are per-call and the global flip setting stays at default.
 * Avoid that unsupported TLS path for every game using this Vita decoder. */
#ifdef __vita__
#define STBI_NO_THREAD_LOCALS
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

struct ifaddrs;

/* SDL2_ttf stubs */
struct TTF_Font {
    /* Keep FT_Face first: mkxp-z intentionally obtains it from TTF_Font. */
    FT_Face face;
    unsigned char *data;
    size_t data_size;
    int point_size;
    int style;
    int outline;
    int kerning;
    int hinting;
};

static FT_Library vita_ft_library;
static int vita_ft_ready;
static char vita_ttf_error[256];

static int vita_ttf_init_library(void) {
    if (vita_ft_ready)
        return 0;
    if (FT_Init_FreeType(&vita_ft_library)) {
        snprintf(vita_ttf_error, sizeof(vita_ttf_error), "FreeType init failed");
        return -1;
    }
    vita_ft_ready = 1;
    return 0;
}

static void vita_ttf_set_error(const char *message) {
    snprintf(vita_ttf_error, sizeof(vita_ttf_error), "%s", message ? message : "");
}

static uint32_t vita_read_utf8(const char **text) {
    const unsigned char *p = (const unsigned char *)*text;
    uint32_t codepoint;
    if (!*p)
        return 0;
    if (p[0] < 0x80) {
        *text += 1;
        return p[0];
    }
    if ((p[0] & 0xe0) == 0xc0 && p[1]) {
        codepoint = ((uint32_t)(p[0] & 0x1f) << 6) | (p[1] & 0x3f);
        *text += 2;
        return codepoint;
    }
    if ((p[0] & 0xf0) == 0xe0 && p[1] && p[2]) {
        codepoint = ((uint32_t)(p[0] & 0x0f) << 12) |
                    ((uint32_t)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        *text += 3;
        return codepoint;
    }
    if ((p[0] & 0xf8) == 0xf0 && p[1] && p[2] && p[3]) {
        codepoint = ((uint32_t)(p[0] & 0x07) << 18) |
                    ((uint32_t)(p[1] & 0x3f) << 12) |
                    ((uint32_t)(p[2] & 0x3f) << 6) | (p[3] & 0x3f);
        *text += 4;
        return codepoint;
    }
    *text += 1;
    return 0xfffd;
}

static int vita_utf8_metrics(TTF_Font *font, const char *text, int *width, int *height) {
    const char *p = text ? text : "";
    int pen = 0;
    int max_top = 0;
    int max_bottom = 0;
    while (*p) {
        uint32_t codepoint = vita_read_utf8(&p);
        FT_UInt glyph = FT_Get_Char_Index(font->face, codepoint);
        if (FT_Load_Glyph(font->face, glyph, FT_LOAD_DEFAULT))
            continue;
        pen += (int)(font->face->glyph->advance.x >> 6);
        if (font->face->glyph->metrics.horiBearingY >> 6 > max_top)
            max_top = (int)(font->face->glyph->metrics.horiBearingY >> 6);
        {
            int bottom = (int)((font->face->glyph->metrics.height -
                                font->face->glyph->metrics.horiBearingY) >> 6);
            if (bottom > max_bottom)
                max_bottom = bottom;
        }
    }
    if (width)
        *width = pen;
    if (height)
        *height = max_top + max_bottom;
    return 0;
}

static SDL_Surface *vita_make_surface(int width, int height) {
    SDL_PixelFormat *format;
    SDL_Surface *surface;
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    format = SDL_AllocFormat(SDL_PIXELFORMAT_ABGR8888);
    if (!format)
        return NULL;
    surface = (SDL_Surface *)calloc(1, sizeof(*surface));
    if (!surface) {
        SDL_FreeFormat(format);
        return NULL;
    }
    surface->format = format;
    surface->w = width;
    surface->h = height;
    surface->pitch = width * 4;
    surface->pixels = calloc((size_t)height, (size_t)surface->pitch);
    if (!surface->pixels) {
        SDL_FreeSurface(surface);
        return NULL;
    }
    surface->clip_rect.x = 0;
    surface->clip_rect.y = 0;
    surface->clip_rect.w = (int16_t)width;
    surface->clip_rect.h = (int16_t)height;
    return surface;
}

static void vita_blend_glyph(SDL_Surface *surface, FT_Bitmap *bitmap,
                             int x, int y, SDL_Color color) {
    int row;
    for (row = 0; row < (int)bitmap->rows; ++row) {
        int col;
        for (col = 0; col < (int)bitmap->width; ++col) {
            int dx = x + col;
            int dy = y + row;
            unsigned char coverage;
            uint8_t *pixel;
            if (dx < 0 || dy < 0 || dx >= surface->w || dy >= surface->h)
                continue;
            coverage = bitmap->buffer[row * bitmap->pitch + col];
            if (!coverage)
                continue;
            pixel = (uint8_t *)surface->pixels + dy * surface->pitch + dx * 4;
            pixel[0] = color.r;
            pixel[1] = color.g;
            pixel[2] = color.b;
            pixel[3] = (uint8_t)(((unsigned int)coverage * color.a) / 255u);
        }
    }
}

static SDL_Surface *vita_render_utf8(TTF_Font *font, const char *text, SDL_Color color) {
    int width = 0;
    int height = 0;
    int baseline;
    int pen = 0;
    const char *p;
    SDL_Surface *surface;
    if (!font || !font->face) {
        vita_ttf_set_error("invalid font");
        return NULL;
    }
    vita_utf8_metrics(font, text, &width, &height);
    baseline = (int)(font->face->size->metrics.ascender >> 6);
    height = height > baseline ? height : (int)(font->face->size->metrics.height >> 6);
    surface = vita_make_surface(width + font->outline * 2 + 2,
                                height + font->outline * 2 + 2);
    if (!surface)
        return NULL;
    p = text ? text : "";
    while (*p) {
        uint32_t codepoint = vita_read_utf8(&p);
        FT_UInt glyph = FT_Get_Char_Index(font->face, codepoint);
        if (FT_Load_Glyph(font->face, glyph, FT_LOAD_RENDER))
            continue;
        vita_blend_glyph(surface, &font->face->glyph->bitmap,
                         pen + font->face->glyph->bitmap_left + font->outline,
                         baseline - font->face->glyph->bitmap_top + font->outline,
                         color);
        pen += (int)(font->face->glyph->advance.x >> 6);
    }
    return surface;
}

/* mkxp-z supplies the canonical FreeType-backed SDL_ttf ABI in
 * src/vita_stubs.cpp.  Keep the local helpers above for the PNG/surface
 * implementation, but do not export a second set of TTF symbols: the link
 * uses --allow-multiple-definition and otherwise mixes open/render paths. */
#if 0
TTF_Font *TTF_OpenFont(const char *f, int p) {
    SDL_RWops *ops = SDL_RWFromFile(f, "rb");
    if (!ops)
        return NULL;
    return TTF_OpenFontRW(ops, 1, p);
}

TTF_Font *TTF_OpenFontRW(SDL_RWops *s, int freesrc, int p) {
    Sint64 size;
    size_t read_size;
    TTF_Font *font;
    FT_Face face = NULL;
    if (vita_ttf_init_library() != 0 || !s) {
        if (freesrc && s) SDL_RWclose(s);
        return NULL;
    }
    size = SDL_RWsize(s);
    if (size <= 0 || size > (Sint64)SIZE_MAX) {
        vita_ttf_set_error("invalid font stream size");
        if (freesrc) SDL_RWclose(s);
        return NULL;
    }
    read_size = (size_t)size;
    font = (TTF_Font *)calloc(1, sizeof(*font));
    if (!font) {
        if (freesrc) SDL_RWclose(s);
        return NULL;
    }
    font->data = (unsigned char *)malloc(read_size);
    font->data_size = read_size;
    if (!font->data || SDL_RWseek(s, 0, RW_SEEK_SET) < 0 ||
        SDL_RWread(s, font->data, 1, read_size) != read_size ||
        FT_New_Memory_Face(vita_ft_library, font->data, (FT_Long)read_size, 0, &face)) {
        vita_ttf_set_error("unable to read font");
        if (freesrc) SDL_RWclose(s);
        free(font->data);
        free(font);
        return NULL;
    }
    if (freesrc)
        SDL_RWclose(s);
    font->face = face;
    font->point_size = p > 0 ? p : 16;
    font->kerning = 1;
    if (FT_Set_Pixel_Sizes(font->face, 0, (unsigned int)font->point_size)) {
        TTF_CloseFont(font);
        vita_ttf_set_error("unable to set font size");
        return NULL;
    }
    return font;
}
int TTF_GetFontStyle(TTF_Font *f) { return f ? f->style : 0; }
void TTF_SetFontStyle(TTF_Font *f, int s) { if (f) f->style = s; }
int TTF_FontHeight(TTF_Font *f) { return f && f->face ? (int)(f->face->size->metrics.height >> 6) : 0; }
void TTF_CloseFont(TTF_Font *f) {
    if (!f) return;
    if (f->face) FT_Done_Face(f->face);
    free(f->data);
    free(f);
}
int TTF_Init(void) { return vita_ttf_init_library(); }
void TTF_Quit(void) {
    if (vita_ft_ready) {
        FT_Done_FreeType(vita_ft_library);
        vita_ft_ready = 0;
    }
}
int TTF_SizeUTF8(TTF_Font *f, const char *t, int *w, int *h) {
    if (!f || !f->face) return -1;
    return vita_utf8_metrics(f, t, w, h);
}
void TTF_SetFontKerning(TTF_Font *f, int a) { if (f) f->kerning = a; }
int TTF_GetFontOutline(TTF_Font *f) { return f ? f->outline : 0; }
void TTF_SetFontOutline(TTF_Font *f, int o) { if (f) f->outline = o; }
const char *TTF_FontFaceFamilyName(TTF_Font *f) {
    return f && f->face && f->face->family_name ? f->face->family_name : "Vita Sans";
}
const char *TTF_FontFaceStyleName(TTF_Font *f) {
    return f && f->face && f->face->style_name ? f->face->style_name : "Regular";
}
int TTF_SetFontSize(TTF_Font *f, int p) {
    if (!f || !f->face || FT_Set_Pixel_Sizes(f->face, 0, p > 0 ? (unsigned int)p : 1))
        return -1;
    f->point_size = p;
    return 0;
}
void TTF_SetFontHinting(TTF_Font *f, int h) { if (f) f->hinting = h; }
int TTF_GlyphMetrics(TTF_Font *f, Uint16 ch, int *mnx, int *mxx, int *mny, int *mxy, int *adv) {
    if (!f || !f->face || FT_Load_Char(f->face, ch, FT_LOAD_DEFAULT)) return -1;
    if (mnx) *mnx = (int)(f->face->glyph->metrics.horiBearingX >> 6);
    if (mxx) *mxx = (int)((f->face->glyph->metrics.horiBearingX + f->face->glyph->metrics.width) >> 6);
    if (mny) *mny = (int)((f->face->glyph->metrics.horiBearingY - f->face->glyph->metrics.height) >> 6);
    if (mxy) *mxy = (int)(f->face->glyph->metrics.horiBearingY >> 6);
    if (adv) *adv = (int)(f->face->glyph->advance.x >> 6);
    return 0;
}
int TTF_MeasureUTF8(TTF_Font *f, const char *t, int mw, int *ext, int *cnt) {
    int width = 0;
    int count = 0;
    const char *p = t ? t : "";
    if (!f || !f->face) return -1;
    while (*p) {
        const char *before = p;
        uint32_t codepoint = vita_read_utf8(&p);
        FT_UInt glyph = FT_Get_Char_Index(f->face, codepoint);
        if (FT_Load_Glyph(f->face, glyph, FT_LOAD_DEFAULT)) continue;
        width += (int)(f->face->glyph->advance.x >> 6);
        if (mw > 0 && width > mw) { p = before; break; }
        ++count;
    }
    if (ext) *ext = width;
    if (cnt) *cnt = count;
    return 0;
}
SDL_Surface *TTF_RenderUTF8_Blended(TTF_Font *f, const char *t, SDL_Color c) {
    return vita_render_utf8(f, t, c);
}
SDL_Surface *TTF_RenderUTF8_Solid(TTF_Font *f, const char *t, SDL_Color c) {
    return vita_render_utf8(f, t, c);
}
#endif

/* mkxp-z links its real SDL_sound/OpenAL compatibility implementation.  Keep
 * these historical no-op definitions available only for explicitly isolated
 * SDL probes; exporting them from libSDL2 silently overrides real game audio
 * because SDL appears earlier in the static link group. */
#ifdef SDL_VITA_ENABLE_AUDIO_STUBS

/* Sound_Sample forward declaration (used by Sound_Seek) */
struct Sound_Sample;
typedef struct Sound_Sample Sound_Sample;

/* Sound_AudioInfo struct needed by mkxp-z */
typedef struct {
    Uint16 format;
    Uint8 channels;
    Uint8 rate;
} Sound_AudioInfo;

/* SDL_sound stubs */
int Sound_Init(void) { return 1; }
void Sound_Quit(void) {}
const char *Sound_GetError(void) { return ""; }
Sound_Sample *Sound_NewSample(void *rw, const char *ext, Sound_AudioInfo *desired, Uint32 bufferSize) { (void)rw;(void)ext;(void)desired;(void)bufferSize; return (Sound_Sample*)1; }
void Sound_FreeSample(Sound_Sample *sample) { (void)sample; }
Uint32 Sound_Decode(Sound_Sample *sample) { (void)sample; return 0; }
Uint32 Sound_DecodeAll(Sound_Sample *sample) { (void)sample; return 0; }
void Sound_Rewind(Sound_Sample *sample) { (void)sample; }
int Sound_Seek(Sound_Sample *sample, Uint32 ms) { (void)sample;(void)ms; return 0; }

/* OpenAL stubs */
typedef void ALCdevice;
typedef void ALCcontext;
typedef int ALCenum;
typedef int ALCsizei;
typedef int ALCint;
typedef unsigned int ALCuint;
typedef char ALCchar;
#define ALC_TRUE 1
#define ALC_FALSE 0
#define ALC_NO_ERROR 0
ALCdevice *alcOpenDevice(const char *d) { (void)d; return (ALCdevice*)1; }
int alcCloseDevice(ALCdevice *d) { (void)d; return 1; }
ALCcontext *alcCreateContext(ALCdevice *d, const ALCint *a) { (void)d;(void)a; return (ALCcontext*)1; }
int alcMakeContextCurrent(ALCcontext *c) { (void)c; return 1; }
void alcDestroyContext(ALCcontext *c) { (void)c; }
ALCcontext *alcGetCurrentContext(void) { return (ALCcontext*)1; }
ALCdevice *alcGetContextsDevice(ALCcontext *c) { (void)c; return (ALCdevice*)1; }
ALCenum alcGetError(ALCdevice *d) { (void)d; return 0; }
const ALCchar *alcGetString(ALCdevice *d, ALCenum p) { (void)d;(void)p; return ""; }
void alcGetIntegerv(ALCdevice *d, ALCenum p, ALCsizei s, ALCint *v) { (void)d;(void)p;(void)s; if(v)*v=0; }
int alcIsExtensionPresent(ALCdevice *d, const ALCchar *e) { (void)d;(void)e; return 0; }
void *alcGetProcAddress(ALCdevice *d, const ALCchar *f) { (void)d;(void)f; return (void*)1; }

typedef unsigned int ALuint;
typedef int ALenum;
typedef int ALint;
typedef float ALfloat;
typedef int ALsizei;
typedef void ALvoid;
typedef char ALchar;
#define AL_NO_ERROR 0
ALenum alGetError(void) { return 0; }
void alGenBuffers(ALsizei n, ALuint *b) { static ALuint c=1; for(ALsizei i=0;i<n;i++) b[i]=c++; }
void alDeleteBuffers(ALsizei n, const ALuint *b) { (void)n;(void)b; }
void alBufferData(ALuint b, ALenum f, const ALvoid *d, ALsizei s, ALsizei fr) { (void)b;(void)f;(void)d;(void)s;(void)fr; }
void alGenSources(ALsizei n, ALuint *s) { static ALuint c=1; for(ALsizei i=0;i<n;i++) s[i]=c++; }
void alDeleteSources(ALsizei n, const ALuint *s) { (void)n;(void)s; }
void alSourcei(ALuint s, ALenum p, ALint v) { (void)s;(void)p;(void)v; }
void alSourcef(ALuint s, ALenum p, ALfloat v) { (void)s;(void)p;(void)v; }
void alSourcePlay(ALuint s) { (void)s; }
void alSourceStop(ALuint s) { (void)s; }
void alSourcePause(ALuint s) { (void)s; }
void alGetSourcei(ALuint s, ALenum p, ALint *v) { (void)s;(void)p; if(v)*v=0; }
void alGetSourcef(ALuint s, ALenum p, ALfloat *v) { (void)s;(void)p; if(v)*v=0.0f; }
void alSourceQueueBuffers(ALuint s, ALsizei n, const ALuint *b) { (void)s;(void)n;(void)b; }
void alSourceUnqueueBuffers(ALuint s, ALsizei n, ALuint *b) { (void)s;(void)n;(void)b; }
const ALchar *alGetString(ALenum p) { (void)p; return ""; }
void alGetIntegerv(ALenum p, ALint *v) { (void)p; if(v)*v=0; }
void alListenerf(ALenum p, ALfloat v) { (void)p;(void)v; }
void alDistanceModel(ALenum m) { (void)m; }

#endif /* SDL_VITA_ENABLE_AUDIO_STUBS */

static char vita_img_error[256];

static void vita_img_set_error(const char *message) {
    snprintf(vita_img_error, sizeof(vita_img_error), "%s", message ? message : "");
}

typedef struct VitaPNGReader {
    const unsigned char *data;
    size_t size;
    size_t position;
} VitaPNGReader;

static void vita_png_read(png_structp png_ptr, png_bytep out, png_size_t length) {
    VitaPNGReader *reader = (VitaPNGReader *)png_get_io_ptr(png_ptr);
    if (!reader || length > reader->size - reader->position) {
        png_error(png_ptr, "PNG read outside input");
        return;
    }
    memcpy(out, reader->data + reader->position, length);
    reader->position += length;
}

static unsigned char *vita_read_rw(SDL_RWops *src, size_t *size_out, int freesrc) {
    Sint64 size;
    unsigned char *data;
    size_t read_size;
    if (!src) return NULL;
    size = SDL_RWsize(src);
    if (size <= 0 || size > (Sint64)SIZE_MAX) {
        if (freesrc) SDL_RWclose(src);
        return NULL;
    }
    read_size = (size_t)size;
    data = (unsigned char *)malloc(read_size);
    if (!data || SDL_RWseek(src, 0, RW_SEEK_SET) < 0 ||
        SDL_RWread(src, data, 1, read_size) != read_size) {
        free(data);
        data = NULL;
    }
    if (freesrc) SDL_RWclose(src);
    if (size_out) *size_out = data ? read_size : 0;
    return data;
}

static SDL_Surface *vita_png_decode(const unsigned char *data, size_t size) {
    png_structp png = NULL;
    png_infop info = NULL;
    png_bytep *rows = NULL;
    SDL_Surface *surface = NULL;
    VitaPNGReader reader = {data, size, 0};
    png_uint_32 width;
    png_uint_32 height;
    int bit_depth;
    int color_type;
    png_uint_32 rowbytes;
    unsigned int row;

    if (size < 8 || png_sig_cmp((png_bytep)data, 0, 8)) {
        vita_img_set_error("not a PNG");
        return NULL;
    }
    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info) {
        vita_img_set_error("libpng initialization failed");
        if (png) png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }
    if (setjmp(png_jmpbuf(png))) {
        vita_img_set_error("PNG decode failed");
        free(rows);
        if (surface) SDL_FreeSurface(surface);
        png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }
    png_set_read_fn(png, &reader, vita_png_read);
    png_read_info(png, info);
    png_get_IHDR(png, info, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_read_update_info(png, info);
    rowbytes = png_get_rowbytes(png, info);
    surface = vita_make_surface((int)width, (int)height);
    if (!surface || rowbytes < width * 4u) {
        vita_img_set_error("invalid PNG dimensions");
        if (surface) SDL_FreeSurface(surface);
        png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }
    rows = (png_bytep *)calloc(height, sizeof(*rows));
    if (!rows) {
        SDL_FreeSurface(surface);
        png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }
    for (row = 0; row < height; ++row)
        rows[row] = (png_bytep)((unsigned char *)surface->pixels + row * surface->pitch);
    png_read_image(png, rows);
    png_read_end(png, NULL);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    return surface;
}

/* SDL_image — PNGs decode through libpng; JPEGs through stb_image.
 * The old stub PNG-decoded everything, so any game asset stored as JPEG
 * (e.g. To the Moon's panoramas) failed with "not a PNG". Dispatch on
 * magic bytes; the `type` hint is ignored because archive lookups often
 * carry no reliable extension. */
static SDL_Surface *vita_jpeg_decode(const unsigned char *data, size_t size) {
    SDL_Surface *surface = NULL;
    unsigned char *pixels = NULL;
    int width = 0, height = 0, channels = 0;
    int x, y;

    if (size < 3 || data[0] != 0xff || data[1] != 0xd8 || data[2] != 0xff) {
        vita_img_set_error("not a JPEG");
        return NULL;
    }
    pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        vita_img_set_error("JPEG decode failed");
        if (pixels) stbi_image_free(pixels);
        return NULL;
    }
    surface = vita_make_surface(width, height);
    if (!surface) {
        vita_img_set_error("invalid JPEG dimensions");
        stbi_image_free(pixels);
        return NULL;
    }
    /* stb delivers RGBA; our surfaces are ABGR8888 — swizzle R<->B. */
    for (y = 0; y < height; ++y) {
        unsigned char *dst = (unsigned char *)surface->pixels + y * surface->pitch;
        const unsigned char *src = pixels + (size_t)y * (size_t)width * 4u;
        for (x = 0; x < width; ++x) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
            dst += 4;
            src += 4;
        }
    }
    stbi_image_free(pixels);
    return surface;
}


int IMG_Init(int flags) { return flags & (IMG_INIT_PNG | IMG_INIT_JPG); }
void IMG_Quit(void) {}
SDL_Surface *IMG_Load(const char *file) {
    SDL_RWops *src = SDL_RWFromFile(file, "rb");
    return src ? IMG_LoadTyped_RW(src, 1, "PNG") : NULL;
}
SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc) { return IMG_LoadTyped_RW(src, freesrc, "PNG"); }
SDL_Surface *IMG_LoadTyped_RW(SDL_RWops *src, int freesrc, const char *type) {
    unsigned char *data;
    size_t size;
    SDL_Surface *surface;
    (void)type;
    data = vita_read_rw(src, &size, freesrc);
    if (!data) {
        vita_img_set_error("unable to read image");
        return NULL;
    }
    if (size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)
        surface = vita_jpeg_decode(data, size);
    else
        surface = vita_png_decode(data, size);
    free(data);
    return surface;
}
int IMG_isGIF(SDL_RWops *s) { (void)s; return 0; }
int IMG_SaveJPG(SDL_Surface *s, const char *f, int q) { (void)s;(void)f;(void)q; return -1; }
int IMG_SavePNG(SDL_Surface *s, const char *f) { (void)s;(void)f; return -1; }
const char *IMG_GetError(void) { return vita_img_error; }

/* SDL_mouse stubs (signatures match SDL_mouse.h) */
SDL_Cursor *SDL_CreateCursor(const Uint8 *d, const Uint8 *m, int w, int h, int hx, int hy) { (void)d;(void)m;(void)w;(void)h;(void)hx;(void)hy; return (SDL_Cursor*)1; }
void SDL_FreeCursor(SDL_Cursor *c) { (void)c; }
void SDL_SetCursor(SDL_Cursor *c) { (void)c; }
SDL_Cursor *SDL_GetCursor(void) { return (SDL_Cursor*)0; }
int SDL_ShowCursor(int t) { (void)t; return 0; }
void SDL_WarpMouseInWindow(SDL_Window *w, int x, int y) { (void)w;(void)x;(void)y; }
int SDL_GetMouseState(int *x, int *y) { if(x)*x=0; if(y)*y=0; return 0; }
int SDL_GetRelativeMouseState(int *x, int *y) { if(x)*x=0; if(y)*y=0; return 0; }
void SDL_SetRelativeMouseMode(SDL_bool e) { (void)e; }

/* Preserve the preference path expected by RGSS save-path handling.
 * Changing this return value requires game-loading and save compatibility tests. */
char *SDL_GetBasePath(void) { return strdup("app0:/"); }
char *SDL_GetPrefPath(const char *o, const char *a) { (void)o; (void)a; return strdup("ux0:/data/"); }

/* SDL_loadso stubs */
void *SDL_LoadObject(const char *sofile) { (void)sofile; return (void*)1; }
void *SDL_LoadFunction(void *handle, const char *name) { (void)handle;(void)name; return (void*)1; }
void SDL_UnloadObject(void *handle) { (void)handle; }

/* SDL_clipboard stubs */
int SDL_SetClipboardText(const char *text) { (void)text; return 0; }
const char *SDL_GetClipboardText(void) { return ""; }
int SDL_HasClipboardText(void) { return 0; }

/* ifaddrs stubs */
int getifaddrs(struct ifaddrs **ifap) { (void)ifap; errno=ENOTSUP; return -1; }
void freeifaddrs(struct ifaddrs *ifa) { (void)ifa; }

/* SDL_atomic stubs */
int SDL_AtomicTryLock(void *l) { (void)l; return 1; }
void SDL_AtomicLock(void *l) { (void)l; }
void SDL_AtomicUnlock(void *l) { (void)l; }

/* SDL_surface — small software-surface implementation used by mkxp-z's
 * font/image conversion path.  VitaGL owns the final GPU surfaces; these
 * objects only need correct pixels, formats, and alpha-aware blits. */
static unsigned int vita_mask_shift(Uint32 mask) {
    unsigned int shift = 0;
    if (!mask) return 0;
    while ((mask & 1u) == 0) {
        mask >>= 1;
        ++shift;
    }
    return shift;
}

static unsigned int vita_mask_bits(Uint32 mask) {
    unsigned int bits = 0;
    while (mask) {
        bits += mask & 1u;
        mask >>= 1;
    }
    return bits;
}

static Uint32 vita_pack_channel(Uint8 value, Uint32 mask) {
    unsigned int bits;
    unsigned int max_value;
    unsigned int scaled;
    if (!mask) return 0;
    bits = vita_mask_bits(mask);
    max_value = bits >= 8 ? 255u : ((1u << bits) - 1u);
    scaled = ((unsigned int)value * max_value + 127u) / 255u;
    return (scaled << vita_mask_shift(mask)) & mask;
}

static Uint8 vita_unpack_channel(Uint32 pixel, Uint32 mask, Uint8 fallback) {
    unsigned int bits;
    unsigned int value;
    unsigned int max_value;
    if (!mask) return fallback;
    bits = vita_mask_bits(mask);
    max_value = bits >= 8 ? 255u : ((1u << bits) - 1u);
    value = (pixel & mask) >> vita_mask_shift(mask);
    return (Uint8)((value * 255u + max_value / 2u) / max_value);
}

static SDL_PixelFormat *vita_alloc_format_masks(Uint32 format,
                                                  int bpp,
                                                  Uint32 rmask,
                                                  Uint32 gmask,
                                                  Uint32 bmask,
                                                  Uint32 amask) {
    SDL_PixelFormat *result = (SDL_PixelFormat *)calloc(1, sizeof(*result));
    if (!result) return NULL;
    if (format == SDL_PIXELFORMAT_UNKNOWN) {
        const Uint32 known[] = {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_ABGR8888,
            SDL_PIXELFORMAT_RGBA8888, SDL_PIXELFORMAT_BGRA8888,
            SDL_PIXELFORMAT_RGB888, SDL_PIXELFORMAT_BGR888,
            SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_BGR565};
        unsigned int i;
        for (i = 0; i < sizeof(known)/sizeof(known[0]); ++i) {
            int depth;
            Uint32 r, g, b, a;
            if (SDL_PixelFormatEnumToMasks(known[i], &depth, &r, &g, &b, &a) == 0 &&
                depth == bpp && r == rmask && g == gmask && b == bmask && a == amask) {
                format = known[i];
                break;
            }
        }
    }
    result->format = format;
    result->BitsPerPixel = (Uint8)bpp;
    result->BytesPerPixel = (Uint8)((bpp + 7) / 8);
    result->Rmask = rmask;
    result->Gmask = gmask;
    result->Bmask = bmask;
    result->Amask = amask;
    result->Rshift = (Uint8)vita_mask_shift(rmask);
    result->Gshift = (Uint8)vita_mask_shift(gmask);
    result->Bshift = (Uint8)vita_mask_shift(bmask);
    result->Ashift = (Uint8)vita_mask_shift(amask);
    result->Rloss = (Uint8)(8 - (vita_mask_bits(rmask) > 8 ? 8 : vita_mask_bits(rmask)));
    result->Gloss = (Uint8)(8 - (vita_mask_bits(gmask) > 8 ? 8 : vita_mask_bits(gmask)));
    result->Bloss = (Uint8)(8 - (vita_mask_bits(bmask) > 8 ? 8 : vita_mask_bits(bmask)));
    result->Aloss = (Uint8)(8 - (vita_mask_bits(amask) > 8 ? 8 : vita_mask_bits(amask)));
    result->refcount = 1;
    return result;
}

int SDL_PixelFormatEnumToMasks(Uint32 fmt, int *bpp, Uint32 *r, Uint32 *g,
                               Uint32 *b, Uint32 *a) {
    int depth = 0;
    Uint32 rmask = 0, gmask = 0, bmask = 0, amask = 0;
    switch (fmt) {
    case SDL_PIXELFORMAT_RGB565:
        depth = 16; rmask = 0xf800; gmask = 0x07e0; bmask = 0x001f; break;
    case SDL_PIXELFORMAT_BGR565:
        depth = 16; rmask = 0x001f; gmask = 0x07e0; bmask = 0xf800; break;
    case SDL_PIXELFORMAT_ARGB8888:
        depth = 32; rmask = 0x00ff0000; gmask = 0x0000ff00; bmask = 0x000000ff; amask = 0xff000000; break;
    case SDL_PIXELFORMAT_RGBA8888:
        depth = 32; rmask = 0xff000000; gmask = 0x00ff0000; bmask = 0x0000ff00; amask = 0x000000ff; break;
    case SDL_PIXELFORMAT_ABGR8888:
        depth = 32; rmask = 0x000000ff; gmask = 0x0000ff00; bmask = 0x00ff0000; amask = 0xff000000; break;
    case SDL_PIXELFORMAT_BGRA8888:
        depth = 32; rmask = 0x00ff0000; gmask = 0x0000ff00; bmask = 0xff000000; amask = 0x000000ff; break;
    case SDL_PIXELFORMAT_RGB888:
        depth = 32; rmask = 0x00ff0000; gmask = 0x0000ff00; bmask = 0x000000ff; break;
    case SDL_PIXELFORMAT_BGR888:
        depth = 32; rmask = 0x000000ff; gmask = 0x0000ff00; bmask = 0x00ff0000; break;
    case SDL_PIXELFORMAT_XRGB8888:
        depth = 32; rmask = 0x00ff0000; gmask = 0x0000ff00; bmask = 0x000000ff; break;
    case SDL_PIXELFORMAT_XBGR8888:
        depth = 32; rmask = 0x000000ff; gmask = 0x0000ff00; bmask = 0x00ff0000; break;
    default:
        return -1;
    }
    if (bpp) *bpp = depth;
    if (r) *r = rmask;
    if (g) *g = gmask;
    if (b) *b = bmask;
    if (a) *a = amask;
    return 0;
}

SDL_PixelFormat *SDL_AllocFormat(Uint32 fmt) {
    int bpp;
    Uint32 r, g, b, a;
    if (SDL_PixelFormatEnumToMasks(fmt, &bpp, &r, &g, &b, &a) != 0) {
        SDL_SetError("unsupported pixel format: %u", (unsigned int)fmt);
        return NULL;
    }
    return vita_alloc_format_masks(fmt, bpp, r, g, b, a);
}

void SDL_FreeFormat(SDL_PixelFormat *format) {
    if (!format) return;
    if (format->palette) {
        free(format->palette->colors);
        free(format->palette);
    }
    free(format);
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 rmask, Uint32 gmask, Uint32 bmask,
                                  Uint32 amask) {
    SDL_Surface *surface;
    int bytes;
    if (width <= 0 || height <= 0 || depth <= 0 || depth > 32) {
        SDL_SetError("invalid surface dimensions or depth: %dx%d/%d", width, height, depth);
        return NULL;
    }
    bytes = (depth + 7) / 8;
    if (bytes != 2 && bytes != 4) {
        SDL_SetError("unsupported surface depth: %d", depth);
        return NULL;
    }
    surface = (SDL_Surface *)calloc(1, sizeof(*surface));
    if (!surface) {
        SDL_SetError("surface allocation failed");
        return NULL;
    }
    surface->format = vita_alloc_format_masks(SDL_PIXELFORMAT_UNKNOWN, depth,
                                               rmask, gmask, bmask, amask);
    if (!surface->format) {
        SDL_SetError("surface format allocation failed");
        free(surface);
        return NULL;
    }
    surface->w = width;
    surface->h = height;
    surface->pitch = width * bytes;
    surface->pixels = calloc((size_t)height, (size_t)surface->pitch);
    if (!surface->pixels) {
        SDL_SetError("surface pixel allocation failed: %dx%d", width, height);
        SDL_FreeSurface(surface);
        return NULL;
    }
    surface->clip_rect.x = 0;
    surface->clip_rect.y = 0;
    surface->clip_rect.w = width;
    surface->clip_rect.h = height;
    surface->flags = flags;
    return surface;
}

void SDL_FreeSurface(SDL_Surface *surface) {
    if (!surface) return;
    if (!(surface->flags & SDL_PREALLOC)) free(surface->pixels);
    SDL_FreeFormat(surface->format);
    free(surface);
}

static Uint32 vita_read_pixel(const SDL_Surface *surface, int x, int y) {
    const Uint8 *row = (const Uint8 *)surface->pixels + y * surface->pitch;
    if (surface->format->BytesPerPixel == 2) {
        Uint16 value;
        memcpy(&value, row + x * 2, sizeof(value));
        return value;
    }
    {
        Uint32 value = 0;
        memcpy(&value, row + x * surface->format->BytesPerPixel,
               surface->format->BytesPerPixel);
        return value;
    }
}

static void vita_write_pixel(SDL_Surface *surface, int x, int y, Uint32 value) {
    Uint8 *row = (Uint8 *)surface->pixels + y * surface->pitch;
    if (surface->format->BytesPerPixel == 2) {
        Uint16 packed = (Uint16)value;
        memcpy(row + x * 2, &packed, sizeof(packed));
        return;
    }
    memcpy(row + x * surface->format->BytesPerPixel, &value,
           surface->format->BytesPerPixel);
}

static void vita_unpack_rgba(const SDL_Surface *surface, Uint32 pixel,
                             Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) {
    *r = vita_unpack_channel(pixel, surface->format->Rmask, 0);
    *g = vita_unpack_channel(pixel, surface->format->Gmask, 0);
    *b = vita_unpack_channel(pixel, surface->format->Bmask, 0);
    *a = vita_unpack_channel(pixel, surface->format->Amask, 255);
}

static Uint32 vita_pack_rgba(const SDL_PixelFormat *format,
                             Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    return vita_pack_channel(r, format->Rmask) |
           vita_pack_channel(g, format->Gmask) |
           vita_pack_channel(b, format->Bmask) |
           vita_pack_channel(a, format->Amask);
}

static void vita_normalize_rect(const SDL_Surface *surface, const SDL_Rect *input,
                                SDL_Rect *output) {
    if (input) *output = *input;
    else {
        output->x = 0; output->y = 0; output->w = surface->w; output->h = surface->h;
    }
    if (output->x < 0) { output->w += output->x; output->x = 0; }
    if (output->y < 0) { output->h += output->y; output->y = 0; }
    if (output->x + output->w > surface->w) output->w = surface->w - output->x;
    if (output->y + output->h > surface->h) output->h = surface->h - output->y;
    if (output->w < 0) output->w = 0;
    if (output->h < 0) output->h = 0;
}

static int vita_blit_internal(SDL_Surface *src, const SDL_Rect *srcrect,
                              SDL_Surface *dst, SDL_Rect *dstrect, int scaled) {
    SDL_Rect source, target, clip;
    int x, y;
    if (!src || !dst || !src->format || !dst->format || !src->pixels || !dst->pixels)
        return -1;
    vita_normalize_rect(src, srcrect, &source);
    if (dstrect) {
        target = *dstrect;
        if (!scaled) { target.w = source.w; target.h = source.h; }
    } else {
        target.x = 0; target.y = 0; target.w = source.w; target.h = source.h;
    }
    if (target.w <= 0 || target.h <= 0) return 0;
    clip = dst->clip_rect;
    if (clip.x < 0) clip.x = 0;
    if (clip.y < 0) clip.y = 0;
    if (clip.x + clip.w > dst->w) clip.w = dst->w - clip.x;
    if (clip.y + clip.h > dst->h) clip.h = dst->h - clip.y;
    for (y = 0; y < target.h; ++y) {
        int dy = target.y + y;
        int sy = source.y + (scaled ? (y * source.h) / target.h : y);
        if (dy < clip.y || dy >= clip.y + clip.h || sy < 0 || sy >= src->h) continue;
        for (x = 0; x < target.w; ++x) {
            int dx = target.x + x;
            int sx = source.x + (scaled ? (x * source.w) / target.w : x);
            Uint8 sr, sg, sb, sa, dr, dg, db, da;
            Uint32 source_pixel, dest_pixel;
            if (dx < clip.x || dx >= clip.x + clip.w || sx < 0 || sx >= src->w) continue;
            source_pixel = vita_read_pixel(src, sx, sy);
            dest_pixel = vita_read_pixel(dst, dx, dy);
            vita_unpack_rgba(src, source_pixel, &sr, &sg, &sb, &sa);
            if (!src->format->Amask || sa == 255) {
                vita_write_pixel(dst, dx, dy, vita_pack_rgba(dst->format, sr, sg, sb, sa));
                continue;
            }
            if (sa == 0) continue;
            vita_unpack_rgba(dst, dest_pixel, &dr, &dg, &db, &da);
            {
                unsigned int inverse = 255u - sa;
                unsigned int out_a = sa + ((unsigned int)da * inverse + 127u) / 255u;
                unsigned int out_r = ((unsigned int)sr * sa + (unsigned int)dr * da * inverse / 255u);
                unsigned int out_g = ((unsigned int)sg * sa + (unsigned int)dg * da * inverse / 255u);
                unsigned int out_b = ((unsigned int)sb * sa + (unsigned int)db * da * inverse / 255u);
                if (out_a) {
                    out_r = (out_r + out_a / 2u) / out_a;
                    out_g = (out_g + out_a / 2u) / out_a;
                    out_b = (out_b + out_a / 2u) / out_a;
                }
                vita_write_pixel(dst, dx, dy,
                                 vita_pack_rgba(dst->format, (Uint8)out_r,
                                                 (Uint8)out_g, (Uint8)out_b,
                                                 (Uint8)out_a));
            }
        }
    }
    if (dstrect) {
        dstrect->w = target.w;
        dstrect->h = target.h;
    }
    return 0;
}

int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect,
                  SDL_Surface *dst, SDL_Rect *dstrect) {
    return vita_blit_internal(src, srcrect, dst, dstrect, 0);
}
int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect,
                    SDL_Surface *dst, SDL_Rect *dstrect) {
    return SDL_UpperBlit(src, srcrect, dst, dstrect);
}
void SDL_LockSurface(SDL_Surface *surface) { if (surface) surface->locked++; }
void SDL_UnlockSurface(SDL_Surface *surface) { if (surface && surface->locked) surface->locked--; }
int SDL_MUSTLOCK(SDL_Surface *surface) { (void)surface; return 0; }

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color) {
    SDL_Rect area;
    int x, y;
    if (!dst || !dst->pixels) return -1;
    vita_normalize_rect(dst, rect, &area);
    for (y = area.y; y < area.y + area.h; ++y)
        for (x = area.x; x < area.x + area.w; ++x)
            vita_write_pixel(dst, x, y, color);
    return 0;
}
int SDL_FillRects(SDL_Surface *dst, const SDL_Rect *rects, int count, Uint32 color) {
    int i;
    if (!rects || count < 0) return -1;
    for (i = 0; i < count; ++i) if (SDL_FillRect(dst, &rects[i], color) != 0) return -1;
    return 0;
}
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b) {
    return format ? vita_pack_rgba(format, r, g, b, 255) : 0;
}
void SDL_GetRGB(Uint32 pixel, const SDL_PixelFormat *format,
                Uint8 *r, Uint8 *g, Uint8 *b) {
    if (!format) return;
    if (r) *r = vita_unpack_channel(pixel, format->Rmask, 0);
    if (g) *g = vita_unpack_channel(pixel, format->Gmask, 0);
    if (b) *b = vita_unpack_channel(pixel, format->Bmask, 0);
}
int SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha) { (void)surface; (void)alpha; return 0; }
int SDL_GetSurfaceAlphaMod(SDL_Surface *surface, Uint8 *alpha) { (void)surface; if (alpha) *alpha = 255; return 0; }
int SDL_SetSurfaceBlendMode(SDL_Surface *surface, int blend_mode) { (void)surface; (void)blend_mode; return 0; }
int SDL_GetSurfaceBlendMode(SDL_Surface *surface, int *blend_mode) { (void)surface; if (blend_mode) *blend_mode = SDL_BLENDMODE_NONE; return 0; }
int SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect,
                   SDL_Surface *dst, SDL_Rect *dstrect) {
    return vita_blit_internal(src, srcrect, dst, dstrect, 1);
}
int SDL_SoftStretchLinear(SDL_Surface *src, const SDL_Rect *srcrect,
                          SDL_Surface *dst, SDL_Rect *dstrect) {
    return SDL_BlitScaled(src, srcrect, dst, dstrect);
}
int SDL_LowerBlit(SDL_Surface *src, SDL_Rect *srcrect,
                  SDL_Surface *dst, SDL_Rect *dstrect) {
    return SDL_UpperBlit(src, srcrect, dst, dstrect);
}
int SDL_LowerBlitScaled(SDL_Surface *src, SDL_Rect *srcrect,
                        SDL_Surface *dst, SDL_Rect *dstrect) {
    return SDL_BlitScaled(src, srcrect, dst, dstrect);
}
int SDL_SaveBMP(SDL_Surface *surface, const char *file) { (void)surface; (void)file; return -1; }
SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *surface, Uint32 fmt, Uint32 flags) {
    int bpp;
    Uint32 r, g, b, a;
    SDL_Surface *converted;
    (void)flags;
    if (!surface) {
        SDL_SetError("surface conversion source is null");
        return NULL;
    }
    if (SDL_PixelFormatEnumToMasks(fmt, &bpp, &r, &g, &b, &a) != 0) {
        SDL_SetError("surface conversion format unsupported: %u", (unsigned int)fmt);
        return NULL;
    }
    converted = SDL_CreateRGBSurface(0, surface->w, surface->h, bpp, r, g, b, a);
    if (!converted) return NULL;
    /* Conversion preserves straight RGBA, including RGB under zero alpha.
     * It is not a blended blit onto an empty destination. */
    converted->format->format = fmt;
    for (int y = 0; y < surface->h; ++y) {
        if (surface->format->BitsPerPixel == bpp &&
            surface->format->Rmask == r && surface->format->Gmask == g &&
            surface->format->Bmask == b && surface->format->Amask == a) {
            memcpy((Uint8 *)converted->pixels + y * converted->pitch,
                   (const Uint8 *)surface->pixels + y * surface->pitch,
                   (size_t)surface->w * converted->format->BytesPerPixel);
        } else {
            for (int x = 0; x < surface->w; ++x) {
                Uint8 red, green, blue, alpha;
                vita_unpack_rgba(surface, vita_read_pixel(surface, x, y),
                                 &red, &green, &blue, &alpha);
                vita_write_pixel(converted, x, y,
                    vita_pack_rgba(converted->format, red, green, blue, alpha));
            }
        }
    }
    return converted;
}
int SDL_ShowSimpleMessageBox(unsigned int f, const char *t, const char *m, void *w) { (void)f;(void)t;(void)m;(void)w; return 0; }

/* Vita display model: exactly one display, 960x544, 60 Hz.
 * All display-mode queries agree on these values; format uses this
 * shim's own SDL_PIXELFORMAT_XRGB8888 define for internal consistency. */
#define VITA_SHIM_DISPLAY_W 960
#define VITA_SHIM_DISPLAY_H 544
#define VITA_SHIM_DISPLAY_HZ 60
static void vita_shim_fill_display_mode(SDL_DisplayMode *m) {
    if (!m) return;
    m->format = SDL_PIXELFORMAT_XRGB8888;
    m->w = VITA_SHIM_DISPLAY_W;
    m->h = VITA_SHIM_DISPLAY_H;
    m->refresh_rate = VITA_SHIM_DISPLAY_HZ;
    m->driverdata = (void*)0;
}
void SDL_SetWindowPosition(SDL_Window *w, int x, int y) { (void)w;(void)x;(void)y; }
int SDL_GetWindowDisplayIndex(SDL_Window *w) { (void)w; return 0; }
int SDL_GetNumVideoDisplays(void) { return 1; }
int SDL_GetDisplayBounds(int i, SDL_Rect *r) {
    if (i != 0 || !r) return -1;
    r->x = 0; r->y = 0; r->w = VITA_SHIM_DISPLAY_W; r->h = VITA_SHIM_DISPLAY_H;
    return 0;
}
int SDL_GetDisplayMode(int i, int mi, SDL_DisplayMode *m) {
    if (i != 0 || mi != 0 || !m) return -1;
    vita_shim_fill_display_mode(m);
    return 0;
}
int SDL_GetDesktopDisplayMode(int i, SDL_DisplayMode *m) {
    if (i != 0 || !m) return -1;
    vita_shim_fill_display_mode(m);
    return 0;
}
int SDL_GetCurrentDisplayMode(int i, SDL_DisplayMode *m) {
    if (i != 0 || !m) return -1;
    vita_shim_fill_display_mode(m);
    return 0;
}
int SDL_GetWindowDisplayMode(SDL_Window *w, SDL_DisplayMode *m) {
    (void)w;
    if (!m) return -1;
    vita_shim_fill_display_mode(m);
    return 0;
}
void SDL_GL_GetDrawableSize(SDL_Window *w, int *x, int *y) { (void)w; if(x)*x=960; if(y)*y=544; }
/* SDL_events stubs */
void SDL_StartTextInput(void) {}
void SDL_StopTextInput(void) {}
void SDL_SetTextInputRect(const struct SDL_Rect *r) { (void)r; }
int SDL_IsTextInputActive(void) { return 0; }

/* SDL_rect stubs */
SDL_bool SDL_PointInRect(const SDL_Point *p, const SDL_Rect *r) { (void)p;(void)r; return SDL_TRUE; }

/* SDL_joystick stubs */
#define SDL_JOYSTICK_POWER_UNKNOWN (-1)
int SDL_JoystickCurrentPowerLevel(SDL_Joystick *joy) { (void)joy; return SDL_JOYSTICK_POWER_UNKNOWN; }

/* SDL_gamecontroller stubs */
int SDL_GameControllerAddMappingsFromRW(void *rw, int f) { (void)rw;(void)f; return 0; }
int SDL_GameControllerAddMapping(const char *m) { (void)m; return 0; }

/* SDL_timer stubs */
int SDL_RemoveTimer(int id) { (void)id; return 1; }
int SDL_AddTimer(unsigned int interval, void *cb, void *p) { (void)interval;(void)cb;(void)p; return 1; }

/* SDL_log stubs */
void SDL_Log(const char *fmt, ...) { (void)fmt; }
void SDL_LogError(int cat, const char *fmt, ...) { (void)cat;(void)fmt; }

/* TTF functions needed later */
void (*TTF_OpenFont_ptr)(void) = NULL;
