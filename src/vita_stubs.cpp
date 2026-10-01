#include "frame_profile.h"
/*
 * vita_stubs.cpp - Stub implementations for missing Vita SDK symbols
 *
 * The VitaSDK ports of SDL2, pixman, SDL_sound, OpenAL, etc.
 * are incomplete or missing certain symbols. This file provides
 * the minimum implementations needed to link mkxp-z for Vita.
 *
 * TWO KINDS OF STUBS:
 * 1. Functions MISSING from libraries (SDL_Atomic*, SDL_MapRGBA, Sound_*, etc.)
 * 2. Functions PRESENT in libraries but with wrong C++ linkage — we
 *    provide extern "C" wrappers to bridge the gap.
 */

/* strnlen — POSIX 2008, needs _GNU_SOURCE on Vita */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <pwd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <errno.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_timer.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
#include "vita_diagnostic.h"
#include "vita_startup_timer.h"

/*
 * _newlib_heap_size_user — Controls newlib heap size.
 * Default is 128MiB. SceCommonDialog needs room for its own heap,
 * so we limit our heap to 64MiB.
 */
unsigned int _newlib_heap_size_user = SCE_KERNEL_128MiB;

/* =================================================================
 * Release-safe startup timer
 * ================================================================= */
namespace {
constexpr const char *kStartupTimingPath =
    "ux0:/data/hardrpg/startup-timing.log";
uint64_t vita_startup_zero_us = 0;
FILE *vita_startup_file = NULL;
volatile int vita_startup_lock = 0;

void vita_startup_lock_file()
{
    while (__sync_lock_test_and_set(&vita_startup_lock, 1))
        ;
}

void vita_startup_unlock_file()
{
    __sync_lock_release(&vita_startup_lock);
}
}

extern "C" void vitaStartupTimerInit(void)
{
    vita_startup_zero_us = sceKernelGetSystemTimeWide();
    mkdir("ux0:/data", 0777);
    mkdir("ux0:/data/hardrpg", 0777);
    vita_startup_file = std::fopen(kStartupTimingPath, "w");
    vitaStartupTimerMark("process_entry");
}

extern "C" uint64_t vitaStartupTimerElapsedUs(void)
{
    const uint64_t now = sceKernelGetSystemTimeWide();
    return vita_startup_zero_us && now >= vita_startup_zero_us
        ? now - vita_startup_zero_us : 0;
}

extern "C" void vitaStartupTimerMark(const char *phase)
{
    const uint64_t elapsed_us = vitaStartupTimerElapsedUs();
    vita_startup_lock_file();
    if (vita_startup_file)
    {
        std::fprintf(vita_startup_file, "%llu.%03llu ms %s\n",
                     static_cast<unsigned long long>(elapsed_us / 1000),
                     static_cast<unsigned long long>(elapsed_us % 1000),
                     phase ? phase : "<null>");
        std::fflush(vita_startup_file);
    }
    sceClibPrintf("[MKXPZ-STARTUP] %llu.%03llu ms %s\n",
                  static_cast<unsigned long long>(elapsed_us / 1000),
                  static_cast<unsigned long long>(elapsed_us % 1000),
                  phase ? phase : "<null>");
    vita_startup_unlock_file();
}

extern "C" void vitaStartupTimerShutdown(void)
{
    vitaStartupTimerMark("process_shutdown");
    vita_startup_lock_file();
    if (vita_startup_file)
    {
        std::fclose(vita_startup_file);
        vita_startup_file = NULL;
    }
    vita_startup_unlock_file();
}

/* =================================================================
 * SDL2 atomic operations — declared in SDL_atomic.h but NOT
 * compiled into the Vita's libSDL2.a. Use GCC builtins for ARM.
 * ================================================================= */
typedef struct { int value; } SDL_atomic_t;

extern "C" {
int SDL_AtomicGet(SDL_atomic_t *a);
void SDL_AtomicSet(SDL_atomic_t *a, int v);
int SDL_AtomicAdd(SDL_atomic_t *a, int v);
}

int SDL_AtomicGet(SDL_atomic_t *a)
{
    return __sync_fetch_and_add(&a->value, 0);
}

void SDL_AtomicSet(SDL_atomic_t *a, int v)
{
    __sync_lock_test_and_set(&a->value, v);
}

int SDL_AtomicAdd(SDL_atomic_t *a, int v)
{
    return __sync_fetch_and_add(&a->value, v);
}

/* =================================================================
 * SDL_MapRGBA — missing from Vita's SDL2 pixel format handling
 * ================================================================= */
extern "C" {
unsigned int SDL_MapRGBA(const SDL_PixelFormat *format,
                         unsigned char r, unsigned char g,
                         unsigned char b, unsigned char a);
}

unsigned int SDL_MapRGBA(const SDL_PixelFormat *format,
                         unsigned char r, unsigned char g,
                         unsigned char b, unsigned char a)
{
    auto pack = [](unsigned char value, unsigned int mask) -> unsigned int {
        if (!mask)
            return 0;
        unsigned int shift = 0;
        unsigned int bits = 0;
        unsigned int maximum;
        while ((mask & 1u) == 0) {
            mask >>= 1;
            ++shift;
        }
        while (mask) {
            bits += mask & 1u;
            mask >>= 1;
        }
        maximum = bits >= 8 ? 255u : ((1u << bits) - 1u);
        return ((((unsigned int)value * maximum + 127u) / 255u) << shift);
    };
    if (!format)
        return 0;
    switch (format->format) {
    case SDL_PIXELFORMAT_ARGB8888:
    case SDL_PIXELFORMAT_ABGR8888:
    case SDL_PIXELFORMAT_RGBA8888:
    case SDL_PIXELFORMAT_BGRA8888:
        return (static_cast<unsigned int>(r) << format->Rshift) |
               (static_cast<unsigned int>(g) << format->Gshift) |
               (static_cast<unsigned int>(b) << format->Bshift) |
               (static_cast<unsigned int>(a) << format->Ashift);
    default:
        break;
    }
    return pack(r, format->Rmask) |
           pack(g, format->Gmask) |
           pack(b, format->Bmask) |
           pack(a, format->Amask);
}

/* =================================================================
 * SDL_GetDisplayMode — missing SDL2 video function
 * ================================================================= */
extern "C" {
int SDL_GetDisplayMode(int displayIndex, int modeIndex, void *mode);
}
int SDL_GetDisplayMode(int displayIndex, int modeIndex, void *mode)
{
    (void)displayIndex;
    (void)modeIndex;
    (void)mode;
    return 0;
}

/* =================================================================
 * SDL_JoystickCurrentPowerLevel — missing from Vita's SDL2
 * ================================================================= */
extern "C" {
int SDL_JoystickCurrentPowerLevel(void *joystick);
}
int SDL_JoystickCurrentPowerLevel(void *joystick)
{
    (void)joystick;
    return -1;
}

/* =================================================================
 * SDL_GetWindowDisplayMode — might be needed
 * ================================================================= */
extern "C" {
int SDL_GetWindowDisplayMode(void *window, void *mode);
}
int SDL_GetWindowDisplayMode(void *window, void *mode)
{
    (void)window;
    (void)mode;
    return 0;
}

/* =================================================================
 * SDL_ttf compatibility layer
 *
 * The Vita SDK's SDL_ttf archive only contains placeholder functions.  mkxp-z
 * also consumes the FreeType face stored at the beginning of TTF_Font, so a
 * sentinel handle is not safe.  Keep the SDL_ttf ABI that mkxp-z uses, but
 * load fonts through the FreeType library already linked for the port.
 * ================================================================= */
struct TTF_Font
{
    /* Keep this first: mkxp-z's font code intentionally obtains the face by
     * reading the first word of SDL_ttf's opaque handle. */
    FT_Face face;
    FT_Library library;
    unsigned char *data;
    size_t data_size;
    int ptsize;
    int style;
    int outline;
    int hinting;
    int kerning;
};

static FT_Library vita_ttf_library = NULL;

/* FreeType does not copy the buffer passed to FT_New_Memory_Face: it must stay
 * alive for the face's entire lifetime.  RPG Maker asks SDL_ttf for the same
 * font at many sizes (and once more for outlined text).  Keeping a private
 * copy in every TTF_Font consumed tens of MiB and left vitaGL unable to create
 * the first New Game render targets.  Retain one immutable backing buffer per
 * distinct font for the process lifetime while still creating an independent
 * FT_Face for each size/style handle.
 *
 * SDL_RWops does not expose the PHYSFS path here, so identify a source using
 * its size plus hashes of its first and last 4 KiB.  The complete bytes are
 * still read on the first miss; repeated opens only touch those two small
 * samples. */
struct VitaTtfBlob
{
    size_t size;
    uint64_t fingerprint;
    unsigned char *data;
};

static std::vector<VitaTtfBlob> vita_ttf_blobs;

static uint64_t vita_ttf_hash_bytes(uint64_t hash,
                                    const unsigned char *data,
                                    size_t size)
{
    const uint64_t fnv_prime = UINT64_C(1099511628211);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= data[i];
        hash *= fnv_prime;
    }
    return hash;
}

static bool vita_ttf_fingerprint(SDL_RWops *src, size_t size,
                                 uint64_t *fingerprint)
{
    const size_t sample_size = std::min<size_t>(size, 4096);
    unsigned char sample[4096];
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = vita_ttf_hash_bytes(hash,
                              reinterpret_cast<const unsigned char *>(&size),
                              sizeof(size));

    if (SDL_RWseek(src, 0, RW_SEEK_SET) < 0 ||
        SDL_RWread(src, sample, 1, sample_size) != sample_size)
        return false;
    hash = vita_ttf_hash_bytes(hash, sample, sample_size);

    if (size > sample_size)
    {
        const Sint64 tail = static_cast<Sint64>(size - sample_size);
        if (SDL_RWseek(src, tail, RW_SEEK_SET) < 0 ||
            SDL_RWread(src, sample, 1, sample_size) != sample_size)
            return false;
        hash = vita_ttf_hash_bytes(hash, sample, sample_size);
    }

    *fingerprint = hash;
    return SDL_RWseek(src, 0, RW_SEEK_SET) >= 0;
}

static unsigned char *vita_ttf_find_blob(size_t size, uint64_t fingerprint)
{
    for (const VitaTtfBlob &blob : vita_ttf_blobs)
        if (blob.size == size && blob.fingerprint == fingerprint)
            return blob.data;
    return NULL;
}

static bool vita_ttf_ensure_library()
{
    return vita_ttf_library || FT_Init_FreeType(&vita_ttf_library) == 0;
}

static std::vector<uint32_t> vita_ttf_decode_utf8(const char *text)
{
    std::vector<uint32_t> codepoints;
    if (!text)
        return codepoints;

    const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text);
    while (*cursor)
    {
        uint32_t codepoint = *cursor++;
        if (codepoint < 0x80)
        {
            codepoints.push_back(codepoint);
            continue;
        }

        int continuation = 0;
        if ((codepoint & 0xE0) == 0xC0) { codepoint &= 0x1F; continuation = 1; }
        else if ((codepoint & 0xF0) == 0xE0) { codepoint &= 0x0F; continuation = 2; }
        else if ((codepoint & 0xF8) == 0xF0) { codepoint &= 0x07; continuation = 3; }
        else { codepoints.push_back(0xFFFD); continue; }

        bool valid = true;
        for (int i = 0; i < continuation; ++i)
        {
            if ((cursor[0] & 0xC0) != 0x80)
            {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | (*cursor++ & 0x3F);
        }
        codepoints.push_back(valid ? codepoint : 0xFFFD);
        if (!valid)
            while ((*cursor & 0xC0) == 0x80)
                ++cursor;
    }
    return codepoints;
}

static int vita_ttf_set_size(TTF_Font *font, int ptsize)
{
    if (!font || !font->face || ptsize <= 0)
        return -1;
    if (FT_Set_Pixel_Sizes(font->face, 0, static_cast<FT_UInt>(ptsize)) != 0)
        return -1;
    font->ptsize = ptsize;
    return 0;
}

/* SDL_ttf-parity text layout.
 *
 * Surfaces are sized by INK BOUNDS (min/max over rasterized glyph boxes),
 * not by summed advances. Display fonts (Bangers, Luckiest Guy) draw ink
 * past the advance edge; advance-sized surfaces clipped 2-4 px off every
 * per-character draw. Pen evolution matches the measurement so Yanfly-style
 * per-character placement lands exactly where the surface expects it.
 *
 * Outline (TTF_SetFontOutline) renders through the FreeType stroker with
 * round caps/joins, the same construction real SDL_ttf uses. The stroked
 * raster already contains the outline expansion, so bounds need no analytic
 * growth on top. */

struct VitaTtfPlacedGlyph
{
    FT_UInt index;
    int pos_x;
    int left;
    int top;
    unsigned int width;
    unsigned int rows;
    int pitch;
    std::vector<unsigned char> coverage;
};

struct VitaTtfLayout
{
    std::vector<VitaTtfPlacedGlyph> glyphs;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int pen_end;
    bool has_ink;
};

static bool vita_ttf_layout(TTF_Font *font,
                            const std::vector<uint32_t> &codepoints,
                            bool rasterize, VitaTtfLayout *layout)
{
    if (!font || !font->face || !layout)
        return false;

    layout->glyphs.clear();
    layout->min_x = 0;
    layout->max_x = 0;
    layout->min_y = 0;
    layout->max_y = 0;
    layout->pen_end = 0;
    layout->has_ink = false;

    if (codepoints.empty())
        return true;

    const int outline = font->outline > 0 ? font->outline : 0;
    const int ascender = font->face->size
        ? static_cast<int>(font->face->size->metrics.ascender >> 6)
        : font->ptsize;

    FT_Pos pen = 0;
    FT_UInt previous = 0;
    bool first_box = true;

    for (uint32_t codepoint : codepoints)
    {
        FT_UInt glyph = FT_Get_Char_Index(font->face, codepoint);
        if (font->kerning && previous && glyph)
        {
            FT_Vector kerning = {};
            if (FT_Get_Kerning(font->face, previous, glyph,
                               FT_KERNING_DEFAULT, &kerning) == 0)
                pen += kerning.x;
        }
        const int pos_x = static_cast<int>((pen + 32) & ~static_cast<FT_Pos>(63)) >> 6;

        int left = 0;
        int top = 0;
        unsigned int width = 0;
        unsigned int rows = 0;
        int pitch = 0;
        /* Unstroked measure box: bounds always derive from this, even when
         * the output pixels come from the stroker. */
        int m_left = 0;
        int m_top = 0;
        unsigned int m_width = 0;
        unsigned int m_rows = 0;
        std::vector<unsigned char> coverage;

        if (FT_Load_Glyph(font->face, glyph, FT_LOAD_DEFAULT) == 0)
        {
            if (rasterize)
            {
                /* Measure from the UNSTROKED raster; the surface then grows
                 * by `outline` per side (real SDL_ttf geometry), so the
                 * outline fringe sits one row inside the edge instead of on
                 * it, where the blend/source-crop trims eat it. Sizing from
                 * the stroked raster cut the bottom outline row. */
                const bool want_stroke =
                    outline > 0 &&
                    font->face->glyph->format == FT_GLYPH_FORMAT_OUTLINE;
                FT_Glyph outline_copy = NULL;
                if (want_stroke)
                {
                    if (FT_Get_Glyph(font->face->glyph, &outline_copy) != 0)
                        outline_copy = NULL;
                }

                bool have_bitmap = false;
                FT_Bitmap *bitmap = NULL;
                if (FT_Render_Glyph(font->face->glyph,
                                    FT_RENDER_MODE_NORMAL) == 0)
                {
                    bitmap = &font->face->glyph->bitmap;
                    left = font->face->glyph->bitmap_left;
                    top = font->face->glyph->bitmap_top;
                    have_bitmap = true;
                }

                if (have_bitmap && bitmap)
                {
                    width = bitmap->width;
                    rows = bitmap->rows;
                    pitch = std::abs(bitmap->pitch);
                    m_left = left;
                    m_top = top;
                    m_width = width;
                    m_rows = rows;
                    if (width && rows && bitmap->buffer)
                    {
                        coverage.assign(static_cast<size_t>(rows) *
                                        static_cast<size_t>(width), 0);
                        for (unsigned int y = 0; y < rows; ++y)
                        {
                            const unsigned char *row = bitmap->buffer +
                                y * std::abs(bitmap->pitch);
                            for (unsigned int x = 0; x < width; ++x)
                                coverage[y * width + x] = row[x];
                        }
                    }
                }

                if (outline_copy)
                {
                    FT_Stroker stroker = NULL;
                    bool stroked = false;
                    if (FT_Stroker_New(vita_ttf_library, &stroker) == 0)
                    {
                        FT_Stroker_Set(stroker,
                                       static_cast<FT_Fixed>(outline * 64),
                                       FT_STROKER_LINECAP_ROUND,
                                       FT_STROKER_LINEJOIN_ROUND, 0);
                        if (FT_Glyph_Stroke(&outline_copy, stroker, 1) == 0 &&
                            FT_Glyph_To_Bitmap(&outline_copy,
                                               FT_RENDER_MODE_NORMAL,
                                               0, 1) == 0)
                        {
                            FT_BitmapGlyph bitmap_glyph =
                                reinterpret_cast<FT_BitmapGlyph>(outline_copy);
                            left = bitmap_glyph->left;
                            top = bitmap_glyph->top;
                            width = bitmap_glyph->bitmap.width;
                            rows = bitmap_glyph->bitmap.rows;
                            pitch = std::abs(bitmap_glyph->bitmap.pitch);
                            if (width && rows &&
                                bitmap_glyph->bitmap.buffer)
                            {
                                coverage.assign(
                                    static_cast<size_t>(rows) *
                                    static_cast<size_t>(width), 0);
                                for (unsigned int y = 0; y < rows; ++y)
                                {
                                    const unsigned char *row =
                                        bitmap_glyph->bitmap.buffer +
                                        y * std::abs(bitmap_glyph->bitmap.pitch);
                                    for (unsigned int x = 0; x < width; ++x)
                                        coverage[y * width + x] = row[x];
                                }
                            }
                            else
                            {
                                coverage.clear();
                            }
                            stroked = true;
                        }
                        FT_Stroker_Done(stroker);
                    }
                    FT_Done_Glyph(outline_copy);
                    (void)stroked;
                }
            }
            else
            {
                const FT_Glyph_Metrics &metrics =
                    font->face->glyph->metrics;
                left = static_cast<int>(metrics.horiBearingX >> 6);
                top = static_cast<int>(metrics.horiBearingY >> 6);
                width = static_cast<unsigned int>(
                    std::max<FT_Pos>(metrics.width >> 6, 0));
                rows = static_cast<unsigned int>(
                    std::max<FT_Pos>(metrics.height >> 6, 0));
                m_left = left;
                m_top = top;
                m_width = width;
                m_rows = rows;
            }

            if (m_width && m_rows)
            {
                const int box_left = pos_x + m_left;
                const int box_top = ascender - m_top;
                const int box_right = box_left + static_cast<int>(m_width);
                const int box_bottom = box_top + static_cast<int>(m_rows);
                if (first_box)
                {
                    layout->min_x = box_left;
                    layout->max_x = box_right;
                    layout->min_y = box_top;
                    layout->max_y = box_bottom;
                    first_box = false;
                }
                else
                {
                    layout->min_x = std::min(layout->min_x, box_left);
                    layout->max_x = std::max(layout->max_x, box_right);
                    layout->min_y = std::min(layout->min_y, box_top);
                    layout->max_y = std::max(layout->max_y, box_bottom);
                }
                layout->has_ink = true;
            }

            pen += font->face->glyph->advance.x;
            previous = glyph;
        }

        if (rasterize)
        {
            VitaTtfPlacedGlyph placed;
            placed.index = glyph;
            placed.pos_x = pos_x;
            placed.left = left;
            placed.top = top;
            placed.width = width;
            placed.rows = rows;
            placed.pitch = pitch;
            placed.coverage.swap(coverage);
            layout->glyphs.push_back(placed);
        }
    }

    layout->pen_end = static_cast<int>(pen >> 6);

    /* Trailing whitespace carries no ink but still advances the pen
     * (single-space strings must measure non-zero, as real SDL_ttf). */
    if (layout->has_ink)
        layout->max_x = std::max(layout->max_x, layout->pen_end);

    return true;
}

static int vita_ttf_font_height(TTF_Font *font)
{
    if (!font || !font->face || !font->face->size)
        return 0;
    // SDL_ttf's canvas uses the scaled ascender-to-descender span, rounded
    // up, rather than FreeType's line-spacing metric or each string's ink.
    const FT_Pos span = FT_MulFix(font->face->ascender - font->face->descender,
                                  font->face->size->metrics.y_scale);
    return std::max(static_cast<int>((span + 63) >> 6), 1);
}

static int vita_ttf_text_width(TTF_Font *font,
                               const std::vector<uint32_t> &codepoints)
{
    FrameProfile::Scope profile(FrameProfile::TtfMeasure);
    if (!font || !font->face)
        return 0;

    VitaTtfLayout layout;
    if (!vita_ttf_layout(font, codepoints, false, &layout))
        return 0;
    const int grow = std::max(font->outline, 0);
    if (!layout.has_ink)
        return std::max(layout.pen_end, 0) + 2 * grow;
    return std::max(layout.max_x - std::min(layout.min_x, 0), 0) + 2 * grow;
}

/* H7 raster sub-split. Gated by app0:/text-profile.on like TEXTTIME. */
struct TtfSplitTotals {
    uint64_t count;
    double ms[4];
};
static TtfSplitTotals ttfSplitTotals = {};

static SDL_Surface *vita_ttf_render(TTF_Font *font, const char *text,
                                     SDL_Color color)
{
    FrameProfile::Scope profile(FrameProfile::TtfRaster);
    if (!font || !font->face || !text) {
        vitaDiagLog("TTF", "render_invalid font=%p face=%p text=%p",
                    (void *)font, font ? (void *)font->face : nullptr,
                    (const void *)text);
        return NULL;
    }

    const bool ttfSplitEnabled = vitaDiagTextProfileEnabled();
    const double ttfSplitFreq = ttfSplitEnabled
        ? static_cast<double>(SDL_GetPerformanceFrequency()) : 1.0;
    const Uint64 ttfT0 = ttfSplitEnabled ? SDL_GetPerformanceCounter() : 0;
    double ttfElapsed[4] = {};

    const std::vector<uint32_t> codepoints = vita_ttf_decode_utf8(text);
    VitaTtfLayout layout;
    if (!vita_ttf_layout(font, codepoints, true, &layout))
        return NULL;
    int width;
    int height;
    int xstart = 0;
    int ystart = 0;
    int ascender = font->face->size
        ? static_cast<int>(font->face->size->metrics.ascender >> 6)
        : font->ptsize;
    const int grow = font->outline > 0 ? font->outline : 0;
    if (layout.has_ink)
    {
        /* Preserve positive bearings and a shared font baseline. Tight ink
         * surfaces put real strokes inside RGSS's top/left outline crop and
         * vertically shift letters in games drawing character by character.
         * Negative bearings need extra canvas space, not clipped pixels. */
        xstart = -std::min(layout.min_x, 0) + grow;
        ystart = -std::min(layout.min_y, 0) + grow;
        width = std::max(layout.max_x + xstart + grow, 1);
        height = std::max(vita_ttf_font_height(font), layout.max_y) + ystart + grow;
    }
    else
    {
        width = std::max(layout.pen_end + 2 * grow, 1);
        height = std::max(vita_ttf_font_height(font) + 2 * grow, 1);
    }
    Uint64 ttfT1 = 0;
    if (ttfSplitEnabled) {
        ttfT1 = SDL_GetPerformanceCounter();
        ttfElapsed[0] = (ttfT1 - ttfT0) * 1000.0 / ttfSplitFreq;
    }
    vitaDiagLog("TTF", "render_begin font=%p bytes=%lu cps=%lu size=%dx%d",
                (void *)font,
                static_cast<unsigned long>(std::strlen(text)),
                static_cast<unsigned long>(codepoints.size()), width, height);
    SDL_Surface *surface = SDL_CreateRGBSurface(
        0, width, height, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surface) {
        vitaDiagLog("TTF", "render_surface_failed error=%s", SDL_GetError());
        return NULL;
    }
    vitaDiagLog("TTF", "render_surface_created surface=%p format=%p pixels=%p pitch=%d",
                (void *)surface, (void *)surface->format, surface->pixels,
                surface->pitch);

    SDL_FillRect(surface, NULL, SDL_MapRGBA(surface->format, 0, 0, 0, 0));
    vitaDiagLog("TTF", "render_surface_cleared");
    if (ttfSplitEnabled)
        ttfElapsed[1] = (SDL_GetPerformanceCounter() - ttfT1) * 1000.0 / ttfSplitFreq;
    const Uint64 ttfP0 = ttfSplitEnabled ? SDL_GetPerformanceCounter() : 0;
    for (size_t gi = 0; gi < layout.glyphs.size(); ++gi)
    {
        const VitaTtfPlacedGlyph &placed = layout.glyphs[gi];
        if (placed.coverage.empty() || !placed.width || !placed.rows)
            continue;
        vitaDiagLog("TTF", "glyph_rendered cp=%lu width=%u rows=%u pitch=%d",
                    static_cast<unsigned long>(codepoints[gi]),
                    placed.width, placed.rows, placed.pitch);
        const int origin_x = xstart + placed.pos_x + placed.left;
        const int origin_y = ystart + ascender - placed.top;
        for (unsigned int y = 0; y < placed.rows; ++y)
        {
            const int dst_y = origin_y + static_cast<int>(y);
            if (dst_y < 0 || dst_y >= surface->h)
                continue;
            for (unsigned int x = 0; x < placed.width; ++x)
            {
                const int dst_x = origin_x + static_cast<int>(x);
                if (dst_x < 0 || dst_x >= surface->w)
                    continue;
                const Uint8 alpha = static_cast<Uint8>(
                    (static_cast<unsigned int>(placed.coverage[y * placed.width + x]) * color.a) / 255U);
                const Uint32 pixel = SDL_MapRGBA(surface->format, color.r,
                                                 color.g, color.b, alpha);
                *reinterpret_cast<Uint32 *>(
                    static_cast<Uint8 *>(surface->pixels) +
                    dst_y * surface->pitch + dst_x * sizeof(Uint32)) = pixel;
            }
        }
        if (ttfSplitEnabled)
            ttfElapsed[3] += (SDL_GetPerformanceCounter() - ttfP0) * 1000.0 / ttfSplitFreq;
    }
    vitaDiagLog("TTF", "render_end surface=%p pitch=%d", (void *)surface,
                surface->pitch);
    if (ttfSplitEnabled) {
        const uint64_t ttfCount = ++ttfSplitTotals.count;
        for (int i = 0; i < 4; ++i) ttfSplitTotals.ms[i] += ttfElapsed[i];
        if ((ttfCount & (ttfCount - 1)) == 0 || ttfCount % 16 == 0)
            vitaDiagLog("TTFSPLIT", "draws=%llu mean_ms width=%.3f alloc=%.3f glyphft=%.3f pixloop=%.3f",
                        static_cast<unsigned long long>(ttfCount),
                        ttfSplitTotals.ms[0] / ttfCount, ttfSplitTotals.ms[1] / ttfCount,
                        ttfSplitTotals.ms[2] / ttfCount, ttfSplitTotals.ms[3] / ttfCount);
    }
    return surface;
}

extern "C" {
TTF_Font *TTF_OpenFont(const char *file, int ptsize)
{
    if (!file)
        return NULL;
    SDL_RWops *src = SDL_RWFromFile(file, "rb");
    return src ? TTF_OpenFontRW(src, 1, ptsize) : NULL;
}

TTF_Font *TTF_OpenFontRW(SDL_RWops *src, int freesrc, int ptsize)
{
    if (!src) {
        vitaDiagLog("TTF", "open_failed source_null ptsize=%d", ptsize);
        return NULL;
    }
    if (!vita_ttf_ensure_library())
    {
        vitaDiagLog("TTF", "open_failed freetype_init ptsize=%d", ptsize);
        if (freesrc)
            SDL_RWclose(src);
        return NULL;
    }

    const Sint64 size = SDL_RWsize(src);
    {
        static uint64_t lastT = 0;
        uint64_t now = sceKernelGetSystemTimeWide();
        vitaDiagLog("TTF", "open_size size=%lld ptsize=%d us_since_prev=%llu",
                    (long long)size, ptsize,
                    (unsigned long long)(lastT ? now - lastT : 0));
        lastT = now;
    }
    if (size <= 0 || size > static_cast<Sint64>(SIZE_MAX))
    {
        vitaDiagLog("TTF", "open_stream_invalid size=%lld", (long long)size);
        if (freesrc)
            SDL_RWclose(src);
        return NULL;
    }

    const size_t data_size = static_cast<size_t>(size);
    uint64_t fingerprint = 0;
    const bool fingerprint_ok = vita_ttf_fingerprint(src, data_size,
                                                     &fingerprint);
    unsigned char *data = fingerprint_ok
        ? vita_ttf_find_blob(data_size, fingerprint) : NULL;
    const bool cache_hit = data != NULL;

    if (!cache_hit)
    {
        data = static_cast<unsigned char *>(malloc(data_size));
        if (!data)
        {
            vitaDiagLog("TTF", "open_failed malloc size=%lld", (long long)size);
            if (freesrc)
                SDL_RWclose(src);
            return NULL;
        }
        const Sint64 seek_result = SDL_RWseek(src, 0, RW_SEEK_SET);
        const size_t read = seek_result >= 0
            ? SDL_RWread(src, data, 1, data_size) : 0;
        if (read != data_size)
        {
            vitaDiagLog("TTF", "open_failed read expected=%lld got=%zu seek=%lld",
                        (long long)size, read, (long long)seek_result);
            free(data);
            if (freesrc)
                SDL_RWclose(src);
            return NULL;
        }

        /* A non-seekable source cannot take the fast path, but keeping its
         * backing bytes alive is still required by FT_New_Memory_Face. */
        if (!fingerprint_ok)
            fingerprint = vita_ttf_hash_bytes(
                UINT64_C(14695981039346656037), data, data_size);
    }
    if (freesrc)
        SDL_RWclose(src);

    FT_Face face = NULL;
    const FT_Error face_error = FT_New_Memory_Face(
        vita_ttf_library, data, static_cast<FT_Long>(size), 0, &face);
    if (face_error != 0)
    {
        vitaDiagLog("TTF", "open_failed freetype_face size=%lld error=%d",
                    (long long)size, (int)face_error);
        if (!cache_hit)
            free(data);
        return NULL;
    }

    if (!cache_hit)
    {
        vita_ttf_blobs.push_back(VitaTtfBlob{data_size, fingerprint, data});
        size_t blob_total = 0;
        for (const VitaTtfBlob &blob : vita_ttf_blobs)
            blob_total += blob.size;
        vitaDiagLog("TTF", "blob_cache_miss size=%lld entries=%lu total=%lu",
                    (long long)size, (unsigned long)vita_ttf_blobs.size(),
                    (unsigned long)blob_total);
    }
    else
    {
        vitaDiagLog("TTF", "blob_cache_hit size=%lld entries=%lu",
                    (long long)size, (unsigned long)vita_ttf_blobs.size());
    }

    TTF_Font *font = new TTF_Font{};
    font->face = face;
    font->library = vita_ttf_library;
    font->data = data;
    font->data_size = static_cast<size_t>(size);
    font->ptsize = 0;
    font->style = TTF_STYLE_NORMAL;
    font->outline = 0;
    font->hinting = 0;
    font->kerning = 1;
    if (ptsize > 0 && vita_ttf_set_size(font, ptsize) != 0)
    {
        FT_Done_Face(face);
        delete font;
        return NULL;
    }
    vitaDiagLog("TTF", "open_ok font=%p size=%lld ptsize=%d", (void *)font,
                (long long)size, ptsize);
    return font;
}

int TTF_Init(void)
{
    return vita_ttf_ensure_library() ? 0 : -1;
}

void TTF_Quit(void)
{
    /* Font objects own their faces and are destroyed before SDL_ttf quits. */
}

int TTF_FontHeight(TTF_Font *font)
{
    return vita_ttf_font_height(font);
}

int TTF_GetFontStyle(TTF_Font *font)
{
    return font ? font->style : TTF_STYLE_NORMAL;
}

void TTF_SetFontStyle(TTF_Font *font, int style)
{
    if (font)
        font->style = style;
}

int TTF_SizeUTF8(TTF_Font *font, const char *text, int *w, int *h)
{
    if (!font || !font->face || !text) {
        vitaDiagLog("TTF", "size_invalid font=%p face=%p text=%p",
                    (void *)font, font ? (void *)font->face : nullptr,
                    (const void *)text);
        return -1;
    }
    if (w)
    {
        *w = vita_ttf_text_width(font, vita_ttf_decode_utf8(text));
    }
    if (h)
    {
        *h = vita_ttf_font_height(font) + 2 * std::max(font->outline, 0);
    }
    return 0;
}

void TTF_CloseFont(TTF_Font *font)
{
    if (!font)
        return;
    if (font->face)
        FT_Done_Face(font->face);
    /* vita_ttf_blobs owns the immutable backing memory.  It intentionally
     * remains resident until process exit so later sizes can reopen without
     * another full-file read or allocation. */
    delete font;
}

SDL_Surface *TTF_RenderUTF8_Blended(TTF_Font *font, const char *text, SDL_Color fg)
{
    return vita_ttf_render(font, text, fg);
}

SDL_Surface *TTF_RenderUTF8_Solid(TTF_Font *font, const char *text, SDL_Color fg)
{
    return vita_ttf_render(font, text, fg);
}

void TTF_SetFontKerning(TTF_Font *font, int allowed)
{
    if (font)
        font->kerning = allowed != 0;
}

int TTF_GetFontOutline(TTF_Font *font)
{
    return font ? font->outline : 0;
}

void TTF_SetFontOutline(TTF_Font *font, int outline)
{
    if (font)
        font->outline = outline;
}

const char *TTF_FontFaceFamilyName(TTF_Font *font)
{
    return font && font->face ? font->face->family_name : NULL;
}

const char *TTF_FontFaceStyleName(TTF_Font *font)
{
    return font && font->face ? font->face->style_name : NULL;
}

int TTF_SetFontSize(TTF_Font *font, int ptsize)
{
    const int result = vita_ttf_set_size(font, ptsize);
    vitaDiagLog("TTF", "set_size font=%p face=%p ptsize=%d result=%d",
                (void *)font, font ? (void *)font->face : nullptr,
                ptsize, result);
    return result;
}

void TTF_SetFontHinting(TTF_Font *font, int hinting)
{
    if (font)
        font->hinting = hinting;
}

int TTF_GlyphMetrics(TTF_Font *font, Uint16 ch, int *minx, int *maxx,
                     int *miny, int *maxy, int *advance)
{
    if (!font || !font->face || FT_Load_Char(font->face, ch, FT_LOAD_DEFAULT) != 0)
        return -1;
    const FT_Glyph_Metrics &metrics = font->face->glyph->metrics;
    const int outline = font->outline > 0 ? font->outline : 0;
    if (minx) *minx = static_cast<int>(metrics.horiBearingX >> 6);
    if (maxx) *maxx = static_cast<int>((metrics.horiBearingX + metrics.width) >> 6) + 2 * outline;
    if (maxy) *maxy = static_cast<int>(metrics.horiBearingY >> 6) + 2 * outline;
    if (miny) *miny = static_cast<int>((metrics.horiBearingY - metrics.height) >> 6);
    if (advance) *advance = static_cast<int>(metrics.horiAdvance >> 6);
    return 0;
}

int TTF_MeasureUTF8(TTF_Font *font, const char *text, int measure_width,
                    int *extent, int *count)
{
    if (!font || !text)
        return -1;
    const std::vector<uint32_t> codepoints = vita_ttf_decode_utf8(text);
    int width = 0;
    int measured = 0;
    for (uint32_t codepoint : codepoints)
    {
        int glyph_width = 0;
        std::vector<uint32_t> one(1, codepoint);
        glyph_width = vita_ttf_text_width(font, one);
        if (measure_width > 0 && measured > 0 && width + glyph_width > measure_width)
            break;
        width += glyph_width;
        ++measured;
    }
    if (extent) *extent = width;
    if (count) *count = measured;
    return 0;
}
}


/* =================================================================
 * pixman — ARM SIMD/NEON optimizations not compiled into Vita's port
 * ================================================================= */
extern "C" {
void _pixman_implementation_create_arm_simd(void);
void _pixman_implementation_create_arm_neon(void);
}
void _pixman_implementation_create_arm_simd(void) {}
void _pixman_implementation_create_arm_neon(void) {}

/* =================================================================
 * OpenAL — alGetBufferi, missing from Vita's OpenAL port
 * ================================================================= */
extern "C" {
void alGetBufferi(unsigned int buffer, int param, int *value);
}
void alGetBufferi(unsigned int buffer, int param, int *value)
{
    (void)buffer;
    (void)param;
    if (value) *value = 0;
}
