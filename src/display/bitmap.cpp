/*
 ** bitmap.cpp
 **
 ** This file is part of mkxp.
 **
 ** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
 **
 ** mkxp is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 2 of the License, or
 ** (at your option) any later version.
 **
 ** mkxp is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "asset_prefetch.h"
#include "bitmap.h"
#include "frame_profile.h"

#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_rect.h>
#include <SDL_surface.h>

#ifdef __vita__
extern "C" void *vglGetTexDataPointerByID(unsigned int texture);
extern "C" {
extern unsigned int vgl_upload_barrier_count;
extern unsigned long long vgl_upload_barrier_us;
}
#endif

#include <pixman.h>

#include "gl-util.h"
#include "gl-meta.h"
#include "quad.h"
#include "quadarray.h"
#include "transform.h"
#include "vita_diagnostic.h"
#include "exception.h"

#include "sharedstate.h"
#include "glstate.h"
#include "texpool.h"
#include "shader.h"
#include "filesystem.h"
#include "font.h"
#include "eventthread.h"
#include "graphics.h"
#include "system.h"
#include "util/util.h"

#include "debugwriter.h"

#include "sigslot/signal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include "libnsgif/libnsgif.h"
}

#define GUARD_MEGA \
{ \
if (p->megaSurface) \
throw Exception(Exception::MKXPError, \
"Operation not supported for mega surfaces"); \
}

#define GUARD_ANIMATED \
{ \
if (p->animation.enabled) \
throw Exception(Exception::MKXPError, \
"Operation not supported for animated bitmaps"); \
}

#define GUARD_UNANIMATED \
{ \
if (!p->animation.enabled) \
throw Exception(Exception::MKXPError, \
"Operation not supported for static bitmaps"); \
}

#define OUTLINE_SIZE 1

/* Normalize (= ensure width and
 * height are positive) */
static IntRect normalizedRect(const IntRect &rect)
{
    IntRect norm = rect;
    
    if (norm.w < 0)
    {
        norm.w = -norm.w;
        norm.x -= norm.w;
    }
    
    if (norm.h < 0)
    {
        norm.h = -norm.h;
        norm.y -= norm.h;
    }
    
    return norm;
}


// libnsgif loading callbacks, taken pretty much straight from their tests

static void *gif_bitmap_create(int width, int height)
{
    /* ensure a stupidly large bitmap is not created */
    return calloc(width * height, 4);
}


static void gif_bitmap_set_opaque(void *bitmap, bool opaque)
{
    (void) opaque;  /* unused */
    (void) bitmap;  /* unused */
    assert(bitmap);
}


static bool gif_bitmap_test_opaque(void *bitmap)
{
    (void) bitmap;  /* unused */
    assert(bitmap);
    return false;
}


static unsigned char *gif_bitmap_get_buffer(void *bitmap)
{
    assert(bitmap);
    return (unsigned char *)bitmap;
}


static void gif_bitmap_destroy(void *bitmap)
{
    assert(bitmap);
    free(bitmap);
}


static void gif_bitmap_modified(void *bitmap)
{
    (void) bitmap;  /* unused */
    assert(bitmap);
    return;
}

// --------------------

struct BitmapPrivate;
static void clearArm(BitmapPrivate *p);
static void clearRead(BitmapPrivate *p);
static void clearFullOverwrite(BitmapPrivate *p);
static void cpuSync(BitmapPrivate *p);
static void cpuPromote(BitmapPrivate *p);

struct BitmapPrivate
{
    Bitmap *self;
    
    struct {
        int width;
        int height;
        
        bool enabled;
        bool playing;
        bool needsReset;
        bool loop;
        std::vector<TEXFBO> frames;
        float fps;
        int lastFrame;
        double startTime, playTime;
        
        inline unsigned int currentFrameIRaw() {
            if (fps <= 0) return lastFrame;
            return floor(lastFrame + (playTime / (1 / fps)));
        }
        
        unsigned int currentFrameI() {
            if (!playing || needsReset) return lastFrame;
            int i = currentFrameIRaw();
            return (loop) ? fmod(i, frames.size()) : (i > (int)frames.size() - 1) ? (int)frames.size() - 1 : i;
        }
        
        inline TEXFBO &currentFrame() {
            int i = currentFrameI();
            return frames[i];
        }
        
        inline void play() {
            playing = true;
            needsReset = true;
        }
        
        inline void stop() {
            lastFrame = currentFrameI();
            playing = false;
        }
        
        inline void seek(int frame) {
            lastFrame = clamp(frame, 0, (int)frames.size());
        }
        
        void updateTimer() {
            if (needsReset) {
                lastFrame = currentFrameI();
                playTime = 0;
                startTime = shState->runTime();
                needsReset = false;
                return;
            }
            
            playTime = shState->runTime() - startTime;
            return;
        }
    } animation;
    
    sigslot::connection prepareCon;
    
    TEXFBO gl;
    
    Font *font;
    
    /* "Mega surfaces" are a hack to allow Tilesets to be used
     * whose Bitmaps don't fit into a regular texture. They're
     * kept in RAM and will throw an error if they're used in
     * any context other than as Tilesets */
    SDL_Surface *megaSurface;
    
    /* A cached version of the bitmap in client memory, for
     * getPixel calls. Is invalidated any time the bitmap
     * is modified */
    SDL_Surface *surface;
    SDL_PixelFormat *format;
    
    /* The 'tainted' area describes which parts of the
     * bitmap are not cleared, ie. don't have 0 opacity.
     * If we're blitting / drawing text to a cleared part
     * with full opacity, we can disregard any old contents
     * in the texture and blit to it directly, saving
     * ourselves the expensive blending calculation */
    pixman_region16_t tainted;

    // For high-resolution texture replacement.
    Bitmap *selfHires;
    Bitmap *selfLores;
    bool assumingRubyGC;

    /* Deferred full-bitmap clear: logical state only, no GPU work here.
     * Bitmap::clear() sets it; a draw op fuses it by issuing the GL clear
     * after binding the same target, a read op materializes it first, a
     * repeated full clear replaces it, disposal drops it. Partial clears
     * keep immediate behavior. Mutable so const blit sources and const
     * Bitmap::getGLTypes can materialize before sampling. */
    mutable bool pendingClear;
    mutable Vec4 pendingColor;

    /* CPU-composed bitmap (see "CPU-composed bitmaps" below). While set,
     * these pixels are the bitmap; the texture is only a copy for sampling
     * and there is no framebuffer. */
    SDL_Surface *cpu;
    bool cpuDirty;
    IntRect cpuDirtyRect;
    /* Bounding box of everything drawn since the last full clear; pixels
     * outside it are known to be transparent. Lets clear() erase and
     * re-upload only what was drawn in a large, mostly empty bitmap. */
    IntRect cpuContent;
    /* Positions in the lists of CPU-composed bitmaps and of read-back
     * copies, so memory pressure can find and release them. */
    std::list<BitmapPrivate *>::iterator cpuIter, readIter;
    bool readListed;
    /* Frame of the last write to the CPU pixels (see Bitmap::frameTick). */
    unsigned cpuStamp = 0;
    
    BitmapPrivate(Bitmap *self)
    : self(self),
    megaSurface(0),
    selfHires(0),
    selfLores(0),
    surface(0),
    assumingRubyGC(false),
    pendingClear(false),
    pendingColor(),
    cpu(0),
    cpuDirty(false),
    readListed(false)
    {
        format = SDL_AllocFormat(SDL_PIXELFORMAT_ABGR8888);
        
        animation.width = 0;
        animation.height = 0;
        animation.enabled = false;
        animation.playing = false;
        animation.loop = true;
        animation.playTime = 0;
        animation.startTime = 0;
        animation.fps = 0;
        animation.lastFrame = 0;
        
        prepareCon = shState->prepareDraw.connect(&BitmapPrivate::prepare, this);
        
        font = &shState->defaultFont();
        pixman_region_init(&tainted);
    }
    
    ~BitmapPrivate()
    {
        prepareCon.disconnect();
        dropReadback();
        releaseCpuPixels();
        SDL_FreeFormat(format);
        pixman_region_fini(&tainted);
    }
    
    void releaseCpuPixels();
    void dropReadback();

    /* Callers render into or blit from the result: GPU storage is required. */
    TEXFBO &getGLTypes() {
        cpuPromote(this);
        return (animation.enabled) ? animation.currentFrame() : gl;
    }
    
    void prepare()
    {
        if (!animation.enabled || !animation.playing) return;
        
        animation.updateTimer();
    }
    
    void allocSurface()
    {
        surface = SDL_CreateRGBSurface(0, gl.width, gl.height, format->BitsPerPixel,
                                       format->Rmask, format->Gmask,
                                       format->Bmask, format->Amask);
    }
    
    void clearTaintedArea()
    {
        pixman_region_fini(&tainted);
        pixman_region_init(&tainted);
    }
    
    void addTaintedArea(const IntRect &rect)
    {
        IntRect norm = normalizedRect(rect);
        pixman_region_union_rect
        (&tainted, &tainted, norm.x, norm.y, norm.w, norm.h);
    }
    
    void substractTaintedArea(const IntRect &rect)
    {
        if (!touchesTaintedArea(rect))
            return;
        
        pixman_region16_t m_reg;
        pixman_region_init_rect(&m_reg, rect.x, rect.y, rect.w, rect.h);
        
        pixman_region_subtract(&tainted, &m_reg, &tainted);
        
        pixman_region_fini(&m_reg);
    }
    
    bool touchesTaintedArea(const IntRect &rect)
    {
        pixman_box16_t box;
        box.x1 = rect.x;
        box.y1 = rect.y;
        box.x2 = rect.x + rect.w;
        box.y2 = rect.y + rect.h;
        
        pixman_region_overlap_t result =
        pixman_region_contains_rectangle(&tainted, &box);
        
        return result != PIXMAN_REGION_OUT;
    }
    
    void bindTexture(ShaderBase &shader, bool substituteLoresSize = true)
    {
        if (selfHires) {
            selfHires->bindTex(shader, substituteLoresSize);
            return;
        }

        if (animation.enabled) {
            if (selfLores) {
                Debug() << "BUG: High-res BitmapPrivate bindTexture for animations not implemented";
            }

            TEXFBO cframe = animation.currentFrame();
            TEX::bind(cframe.tex);
            shader.setTexSize(Vec2i(cframe.width, cframe.height));
            return;
        }
        /* Sampling this bitmap as a texture observes its pixels. */
        clearRead(this);
        cpuSync(this);
        TEX::bind(gl.tex);
        if (selfLores && substituteLoresSize) {
            shader.setTexSize(Vec2i(selfLores->width(), selfLores->height()));
        }
        else {
            shader.setTexSize(Vec2i(gl.width, gl.height));
        }
    }
    
    void bindFBO()
    {
        cpuPromote(this);
        FBO::bind((animation.enabled) ? animation.currentFrame().fbo : gl.fbo);
    }
    
    void pushSetViewport(ShaderBase &shader) const
    {
        glState.viewport.pushSet(IntRect(0, 0, gl.width, gl.height));
        shader.applyViewportProj();
    }
    
    void popViewport() const
    {
        glState.viewport.pop();
    }
    
    void blitQuad(Quad &quad)
    {
        glState.blend.pushSet(false);
        quad.draw();
        glState.blend.pop();
    }
    
    void fillRect(const IntRect &rect,
                  const Vec4 &color)
    {
        bindFBO();
        
        glState.scissorTest.pushSet(true);
        glState.scissorBox.pushSet(normalizedRect(rect));
        glState.clearColor.pushSet(color);
        
        FBO::clear();
        
        glState.clearColor.pop();
        glState.scissorBox.pop();
        glState.scissorTest.pop();
    }
    
    // HOST-TEST-EXTRACT-BEGIN argbToAbgrInPlace
    /* In-place ARGB8888 -> ABGR8888 swizzle for TTF Blended output.
     * Same pixels, no allocation: R/B channels permuted per 32-bit pixel,
     * alpha preserved bit-exactly (outlines/shadows untouched). Returns
     * false for any other pair so callers fall back to SDL conversion.
     * Self-contained for host extraction tests (SDL API + stdint only). */
    static bool argbToAbgrInPlace(SDL_Surface *surf)
    {
        if (!surf || !surf->pixels || !surf->format ||
            surf->format->format != SDL_PIXELFORMAT_ARGB8888 ||
            surf->format->BitsPerPixel != 32)
            return false;
        if ((surf->flags & SDL_RLEACCEL) != 0)
            return false;
        /* No colorkey guard: neither the engine nor the Vita SDL shim ever
         * sets a color key (SDL_SetColorKey is absent from both trees), so
         * TTF/IMG surfaces reaching here carry none. */
        /* Allocate the replacement format BEFORE touching pixels: if this
         * fails we must leave the surface exactly as found so the generic
         * fallback still sees valid ARGB data. */
        SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ABGR8888);
        if (!fmt)
            return false;
        const int w = surf->w, h = surf->h;
        const size_t pitch = (size_t)surf->pitch;
        for (int y = 0; y < h; ++y)
        {
            uint32_t *row = (uint32_t *)((uint8_t *)surf->pixels + (size_t)y * pitch);
            for (int x = 0; x < w; ++x)
            {
                const uint32_t p = row[x];
                row[x] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p << 16) & 0xFF0000u);
            }
        }
        SDL_FreeFormat(surf->format);
        surf->format = fmt;
        return true;
    }
    // HOST-TEST-EXTRACT-END

    static void ensureFormat(SDL_Surface *&surf, Uint32 format)
    {
        FrameProfile::Scope profile(FrameProfile::FormatConvert);
        if (surf->format->format == format)
            return;

        /* Fast path first: TTF Blended glyphs arrive as ARGB8888 and the
         * engine canonical format is ABGR8888. The in-place swizzle avoids
         * SDL_ConvertSurfaceFormat's allocate/convert/free round-trip. */
        if (format != SDL_PIXELFORMAT_ABGR8888 || !argbToAbgrInPlace(surf))
        {
            SDL_Surface *converted = SDL_ConvertSurfaceFormat(surf, format, 0);
            SDL_FreeSurface(surf);
            surf = converted;
        }
    }
    
    void onModified(bool freeSurface = true)
    {
        if (surface && freeSurface)
            dropReadback();
        
        self->modified();
    }
};

/* Deferred full-bitmap clears. Bitmap::clear() records intent; helpers
 * below enforce the contract: draws fuse (GPU clear piggybacks on the
 * target bind the draw already needs), reads materialize first, repeated
 * full clears replace, disposal discards. */
static void materializeClear(BitmapPrivate *p)
{
    if (!p || !p->pendingClear)
        return;
    p->pendingClear = false;
    if (p->animation.enabled || p->megaSurface)
        return;
    const Vec4 color = p->pendingColor;
    /* Materialization often runs mid-pass (e.g. getGLTypes/blitSource or
     * bindTexture after the caller bound a *different* destination FBO).
     * Save and restore the bound target so the clear cannot redirect the
     * caller's subsequent draws into this bitmap. Viewport does not affect
     * glClear, but an outer scissored scope would clip it, so clear
     * unscissored. */
    const FBO::ID saved = FBO::boundFramebufferID;
    p->bindFBO();
    glState.scissorTest.pushSet(false);
    glState.clearColor.pushSet(color);
    FBO::clear();
    glState.clearColor.pop();
    glState.scissorTest.pop();
    FBO::bind(saved);

    vitaDiagLog("DEBUG-aoni", "materialize tex=%dx%d upbar=%u upbar_us=%llu",
                p->gl.width, p->gl.height,
#ifdef __vita__
                vgl_upload_barrier_count, vgl_upload_barrier_us
#else
                0u, 0ull
#endif
                );

#ifdef __vita__
    /* vitaGL executes glClear on the GPU at the next flush, while CPU
     * texture uploads (uploadSubImage memcpy) land immediately. Without
     * a barrier, this queued clear would execute after uploads that
     * logically follow it, wiping one-shot draws such as window text.
     * glFinish also bumps vitaGL's finish epoch, keeping the COW
     * recently-used tracking exact for the uploads that follow. */
    {
        FrameProfile::Scope syncScope(FrameProfile::ClearSync);
        gl.Finish();
    }
#endif

    p->clearTaintedArea();
}

static void clearArm(BitmapPrivate *p)
{
    if (!p)
        return;
    if (p->pendingClear)
    {
        FrameProfile::Scope replaced(FrameProfile::ClearReplaced);
    }
    p->pendingClear = true;
    p->pendingColor = Vec4();
    FrameProfile::Scope full(FrameProfile::ClearFull);
}

/* Pixels will be observed: sample paths (bindTexture, blit sources,
 * readbacks, filters reading neighbors). Keep tainted bookkeeping and
 * CPU-surface invalidation immediate via the materialized path. */
static void clearRead(BitmapPrivate *p)
{
    if (p && p->pendingClear)
    {
        FrameProfile::Scope materialized(FrameProfile::ClearNextRead);
        materializeClear(p);
    }
}

/* A partial write preserves pixels outside its destination. Materialize
 * the pending full clear first so those pixels cannot retain the previous
 * frame. Only a full overwrite may discard the clear without executing it. */
static void clearBeforePartialWrite(BitmapPrivate *p)
{
    if (p && p->pendingClear)
    {
        FrameProfile::Scope materialized(FrameProfile::ClearNextDraw);
        materializeClear(p);
    }
}

/* Full CPU overwrite without reading prior pixels (replaceRaw): the
 * pending clear is dead, drop it with no GPU work. */
static void clearFullOverwrite(BitmapPrivate *p)
{
    if (p && p->pendingClear)
    {
        p->pendingClear = false;
        FrameProfile::Scope fused(FrameProfile::ClearNextDraw);
    }
}


/* ---- CPU-composed bitmaps ------------------------------------------------
 *
 * A bitmap made with Bitmap.new(width, height) starts as plain pixels in
 * memory. Fills, clears, text, pixel access and blits whose source pixels
 * are available in memory are done on the CPU, and the result is uploaded
 * to a texture once, when something draws the bitmap.
 *
 * On the Vita every write to a GPU bitmap is its own render-target pass, and
 * text adds a texture upload the GPU has to be flushed for. A window refresh
 * with a hundred draw_text calls spent about 220 ms waiting for that; here
 * it is a hundred small memory blends and one upload.
 *
 * The first operation without a CPU version (hue change, blur, a blit the
 * GPU must do, direct use as a render target) hands the bitmap to the GPU
 * for good; after that it behaves like any loaded image. */
#ifdef __vita__
#define MKXPZ_CPU_BITMAPS 1
#endif

/* CPU pixels come from the same heap as Ruby and the game's data, so the
 * total is capped; past it, new bitmaps start on the GPU as before. */
static size_t cpuBitmapBytes = 0;
static const size_t cpuBitmapBudget = 56u * 1024 * 1024;
static std::list<BitmapPrivate *> cpuBitmapList;
static unsigned cpuFrame = 0;
/* The part of cpuBitmapBytes that sits in the C heap: the SDL layer gives
 * buffers of 128 KiB and more a kernel memory block of their own. */
static size_t cpuHeapBytes = 0;
static const size_t cpuHeapResident = 128u * 1024;
static inline size_t cpuHeapShare(size_t bytes) { return bytes < cpuHeapResident ? bytes : 0; }

/* Read-back copies of GPU bitmaps (get_pixel, blit sources), oldest first.
 * They can always be made again, so they are held to a budget and dropped
 * first when memory runs short. */
static size_t readbackBytes = 0;
static const size_t readbackBudget = 12u * 1024 * 1024;
static std::list<BitmapPrivate *> readbackList;

void BitmapPrivate::dropReadback()
{
    if (!surface)
        return;
    if (readListed)
    {
        readbackBytes -= (size_t)surface->w * surface->h * 4;
        readbackList.erase(readIter);
        readListed = false;
    }
    SDL_FreeSurface(surface);
    surface = 0;
}

static void dropAllReadbacks()
{
    while (!readbackList.empty())
        readbackList.front()->dropReadback();
}

static bool cpuBitmapEligible(int width, int height)
{
#ifdef MKXPZ_CPU_BITMAPS
    return !shState->config().enableHires &&
           cpuBitmapBytes + (size_t)width * height * 4 <= cpuBitmapBudget &&
           (int64_t)width * height <= 1024 * 1024 &&
           width <= glState.caps.maxTexSize && height <= glState.caps.maxTexSize;
#else
    (void)width; (void)height;
    return false;
#endif
}

void BitmapPrivate::releaseCpuPixels()
{
    if (!cpu)
        return;
    cpuBitmapBytes -= (size_t)cpu->w * cpu->h * 4;
    cpuHeapBytes -= cpuHeapShare((size_t)cpu->w * cpu->h * 4);
    cpuBitmapList.erase(cpuIter);
    SDL_FreeSurface(cpu);
    cpu = 0;
}

static IntRect cpuClip(const IntRect &rect, int width, int height)
{
    IntRect r = normalizedRect(rect);
    const int x0 = std::max(r.x, 0), y0 = std::max(r.y, 0);
    const int x1 = std::min<int64_t>((int64_t)r.x + r.w, width);
    const int y1 = std::min<int64_t>((int64_t)r.y + r.h, height);
    return IntRect(x0, y0, std::max(x1 - x0, 0), std::max(y1 - y0, 0));
}

/* rect must already lie inside the bitmap. */
static void cpuMarkDirty(BitmapPrivate *p, const IntRect &rect)
{
    if (rect.w <= 0 || rect.h <= 0)
        return;
    p->cpuStamp = cpuFrame;
    if (!p->cpuDirty)
    {
        p->cpuDirty = true;
        p->cpuDirtyRect = rect;
        return;
    }
    const IntRect &d = p->cpuDirtyRect;
    const int x0 = std::min(d.x, rect.x), y0 = std::min(d.y, rect.y);
    const int x1 = std::max(d.x + d.w, rect.x + rect.w);
    const int y1 = std::max(d.y + d.h, rect.y + rect.h);
    p->cpuDirtyRect = IntRect(x0, y0, x1 - x0, y1 - y0);
}

/* A write that may leave visible pixels in rect (already inside the bitmap). */
static void cpuTouch(BitmapPrivate *p, const IntRect &rect)
{
    if (rect.w <= 0 || rect.h <= 0)
        return;
    cpuMarkDirty(p, rect);
    IntRect &c = p->cpuContent;
    if (c.w <= 0 || c.h <= 0)
    {
        c = rect;
        return;
    }
    const int x0 = std::min(c.x, rect.x), y0 = std::min(c.y, rect.y);
    const int x1 = std::max(c.x + c.w, rect.x + rect.w);
    const int y1 = std::max(c.y + c.h, rect.y + rect.h);
    c = IntRect(x0, y0, x1 - x0, y1 - y0);
}

/* The texture must be bound. */
static void cpuUpload(BitmapPrivate *p, const IntRect &rect)
{
    SDL_Surface *s = p->cpu;
    const uint8_t *first = (const uint8_t *)s->pixels + (size_t)rect.y * s->pitch;
    if (rect.x == 0 && rect.w == s->w && s->pitch == s->w * 4)
    {
        TEX::uploadSubImage(0, rect.y, rect.w, rect.h, first, GL_RGBA);
        return;
    }
    static std::vector<uint8_t> rows;
    rows.resize((size_t)rect.w * rect.h * 4);
    for (int y = 0; y < rect.h; ++y)
        memcpy(&rows[(size_t)y * rect.w * 4], first + (size_t)y * s->pitch + (size_t)rect.x * 4,
               (size_t)rect.w * 4);
    TEX::uploadSubImage(rect.x, rect.y, rect.w, rect.h, rows.data(), GL_RGBA);
}

/* Bring the sampling texture up to date; called just before it is bound. */
static void cpuSync(BitmapPrivate *p)
{
    if (!p->cpu || (!p->cpuDirty && p->gl.tex != TEX::ID(0)))
        return;
    if (p->gl.tex == TEX::ID(0))
    {
        p->gl.tex = TEX::gen();
        TEX::bind(p->gl.tex);
        TEX::setRepeat(false);
        TEX::setSmooth(false);
        try
        {
            try
            {
                TEX::allocEmpty(p->gl.width, p->gl.height);
            }
            catch (const Exception &)
            {
                /* Pooled render targets have no owner; give their storage
                 * back and try once more. */
                shState->texPool().clearCache();
                TEX::bind(p->gl.tex);
                TEX::allocEmpty(p->gl.width, p->gl.height);
            }
        }
        catch (const Exception &)
        {
            TEX::del(p->gl.tex);
            p->gl.tex = TEX::ID(0);
            throw;
        }
        p->cpuDirty = true;
        p->cpuDirtyRect = IntRect(0, 0, p->cpu->w, p->cpu->h);
    }
    else
        TEX::bind(p->gl.tex);
    cpuUpload(p, p->cpuDirtyRect);
    p->cpuDirty = false;
}

/* vitaGL moves a texture to new storage when pixels are uploaded while the
 * GPU may still read the old ones, but a framebuffer keeps the address it
 * was attached with: later draws into the bitmap would land in the released
 * storage and never show. Attach again after uploading into a render
 * target. Same size, so the render target itself is kept. */
static void relinkAfterUpload(TEXFBO &target)
{
#ifdef __vita__
    if (target.fbo == FBO::ID(0) || target.tex == TEX::ID(0))
        return;
    const FBO::ID bound = FBO::boundFramebufferID;
    TEXFBO::linkFBO(target);
    FBO::bind(bound);
#else
    (void)target;
#endif
}

/* Hand the bitmap to the GPU: from here on it is an ordinary render target. */
static void cpuPromote(BitmapPrivate *p)
{
    if (!p->cpu)
        return;
    SDL_Surface *pixels = p->cpu;
    /* This can run in the middle of a draw pass (a blit that samples this
     * bitmap). Creating a render target binds its framebuffer, so put the
     * caller's target back afterwards. */
    const FBO::ID bound = FBO::boundFramebufferID;
    /* May throw; the bitmap is still intact and CPU-composed then. */
    TEXFBO full = shState->texPool().request(pixels->w, pixels->h);
    FBO::bind(bound);
    if (p->gl.tex != TEX::ID(0))
        TEX::del(p->gl.tex);
    p->gl = full;
    p->gl.selfHires = nullptr;
    p->cpuDirty = false;
    TEX::bind(p->gl.tex);
    TEX::uploadSubImage(0, 0, pixels->w, pixels->h, pixels->pixels, GL_RGBA);
    relinkAfterUpload(p->gl);
    p->releaseCpuPixels();
    /* Which pixels are still transparent is no longer tracked. */
    p->clearTaintedArea();
    p->addTaintedArea(IntRect(0, 0, p->gl.width, p->gl.height));
}

static inline uint32_t cpuPack(const Vec4 &c)
{
    const uint32_t r = (uint32_t)lround(clamp<float>(c.x, 0, 1) * 255.0f);
    const uint32_t g = (uint32_t)lround(clamp<float>(c.y, 0, 1) * 255.0f);
    const uint32_t b = (uint32_t)lround(clamp<float>(c.z, 0, 1) * 255.0f);
    const uint32_t a = (uint32_t)lround(clamp<float>(c.w, 0, 1) * 255.0f);
    return r | (g << 8) | (b << 16) | (a << 24);
}

/* rect must already lie inside the surface. */
static void cpuFill(SDL_Surface *s, const IntRect &rect, uint32_t pixel)
{
    for (int y = rect.y; y < rect.y + rect.h; ++y)
        std::fill_n((uint32_t *)((uint8_t *)s->pixels + (size_t)y * s->pitch) + rect.x, rect.w, pixel);
}

/* The colour ramp is defined on the requested rect and sampled at pixel
 * centres, like the interpolated quad the GPU path draws. */
/* The colour only changes along one axis, so each column (horizontal) or
 * row (vertical) is packed once and copied: a 352x168 window background
 * took 20 ms per pixel on the Vita and now takes well under 1 ms. */
static uint32_t cpuGradientAt(int offset, int extent, const Vec4 &c1, const Vec4 &c2)
{
    const float t = (offset + 0.5f) / extent;
    return cpuPack(Vec4(c1.x + (c2.x - c1.x) * t, c1.y + (c2.y - c1.y) * t,
                        c1.z + (c2.z - c1.z) * t, c1.w + (c2.w - c1.w) * t));
}

static void cpuGradient(SDL_Surface *s, const IntRect &rect, const IntRect &clip,
                        const Vec4 &c1, const Vec4 &c2, bool vertical)
{
    if (clip.w <= 0 || clip.h <= 0)
        return;
    const int extent = vertical ? rect.h : rect.w;
    if (vertical)
    {
        for (int y = clip.y; y < clip.y + clip.h; ++y)
            std::fill_n((uint32_t *)((uint8_t *)s->pixels + (size_t)y * s->pitch) + clip.x, clip.w,
                        cpuGradientAt(y - rect.y, extent, c1, c2));
        return;
    }
    uint32_t *first = (uint32_t *)((uint8_t *)s->pixels + (size_t)clip.y * s->pitch) + clip.x;
    for (int x = 0; x < clip.w; ++x)
        first[x] = cpuGradientAt(clip.x + x - rect.x, extent, c1, c2);
    for (int y = clip.y + 1; y < clip.y + clip.h; ++y)
        std::memcpy((uint32_t *)((uint8_t *)s->pixels + (size_t)y * s->pitch) + clip.x, first,
                    (size_t)clip.w * 4);
}

/* 16.16 reciprocals of 1..255, so the blend needs no division (the Cortex-A9
 * has no integer divide instruction). */
static const uint32_t *cpuReciprocals()
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 1; i < 256; ++i)
            table[i] = (65536 + i / 2) / i;
    return table;
}

/* Source over destination for straight (not premultiplied) RGBA, the same
 * result as the bitmapBlit shader: a = sa + da(1 - sa),
 * rgb = (src*sa + dst*da(1 - sa)) / a. */
static inline uint32_t cpuOver(uint32_t d, uint32_t s, uint32_t opacity, const uint32_t *recip)
{
    uint32_t sa = s >> 24;
    if (opacity != 255)
        sa = (sa * opacity + 127) / 255;
    if (sa == 0)
        return d;
    const uint32_t da = d >> 24;
    if (sa == 255 || da == 0)
        return (s & 0x00FFFFFF) | (sa << 24);
    const uint32_t co2 = (da * (255 - sa) + 127) / 255;
    const uint32_t ra = sa + co2;
    const uint32_t inv = recip[ra];
    uint32_t out = ra << 24;
    for (int shift = 0; shift < 24; shift += 8)
    {
        const uint32_t v = ((((s >> shift) & 0xFF) * sa + ((d >> shift) & 0xFF) * co2) * inv + 0x8000) >> 16;
        out |= (v > 255 ? 255 : v) << shift;
    }
    return out;
}

static inline uint32_t cpuLerp(uint32_t a, uint32_t b, uint32_t f)
{
    /* f is 0..256 */
    const uint32_t rb = ((a & 0x00FF00FF) * (256 - f) + (b & 0x00FF00FF) * f) >> 8;
    const uint32_t ga = (((a >> 8) & 0x00FF00FF) * (256 - f) + ((b >> 8) & 0x00FF00FF) * f);
    return (rb & 0x00FF00FF) | (ga & 0xFF00FF00);
}

/* A copy of a GPU bitmap's pixels in memory, kept until the bitmap changes.
 * The caller has materialized any pending clear. */
static bool readbackSurface(BitmapPrivate *p)
{
    if (p->surface)
    {
        if (p->readListed)
            readbackList.splice(readbackList.end(), readbackList, p->readIter);
        return true;
    }
    p->allocSurface();
    if (!p->surface)
    {
        dropAllReadbacks();
        p->allocSurface();
        if (!p->surface)
            return false;
    }
    readbackBytes += (size_t)p->surface->w * p->surface->h * 4;
    p->readIter = readbackList.insert(readbackList.end(), p);
    p->readListed = true;
    while (readbackBytes > readbackBudget && readbackList.front() != p)
        readbackList.front()->dropReadback();

#ifdef __vita__
        /*
         * vitaGL keeps ordinary RGBA8 texture storage in CPU-visible mapped
         * memory.  Reading that storage directly is both cheaper and safer
         * than glReadPixels on Vita: the latter can return zeroes for an FBO
         * texture (breaking VX Ace's windowskin colour sampling) and has also
         * faulted the physical Vita GPU in diagnostic captures.
         */
        glFinish();
        const Uint8 *texturePixels = static_cast<const Uint8 *>(
            vglGetTexDataPointerByID(p->gl.tex.gl));
        if (texturePixels)
        {
            const size_t sourcePitch =
                static_cast<size_t>((p->gl.width + 7) & ~7) * 4;
            const size_t copyWidth = static_cast<size_t>(p->gl.width) * 4;
            Uint8 *surfacePixels = static_cast<Uint8 *>(p->surface->pixels);
            for (int row = 0; row < p->gl.height; ++row)
                std::memcpy(surfacePixels + row * p->surface->pitch,
                            texturePixels + row * sourcePitch, copyWidth);
        }
        else
        {
            vitaDiagLog("BITMAP", "get_pixel_texture_data_missing tex=%u size=%dx%d",
                        p->gl.tex.gl, p->gl.width, p->gl.height);
            FBO::bind(p->gl.fbo);
            glState.viewport.pushSet(IntRect(0, 0, p->gl.width, p->gl.height));
            gl.ReadPixels(0, 0, p->gl.width, p->gl.height, GL_RGBA, GL_UNSIGNED_BYTE,
                          p->surface->pixels);
            glState.viewport.pop();
        }
#else
        FBO::bind(p->gl.fbo);
        glState.viewport.pushSet(IntRect(0, 0, p->gl.width, p->gl.height));
        gl.ReadPixels(0, 0, p->gl.width, p->gl.height, GL_RGBA, GL_UNSIGNED_BYTE, p->surface->pixels);
        glState.viewport.pop();
#endif
    return true;
}

/* Pixels of a blit source in memory, or null when only the GPU can do the
 * blit. Loaded images are read back once and the copy is kept until they
 * change; very large ones are left to the GPU. */
static SDL_Surface *cpuSourcePixels(const Bitmap &source, BitmapPrivate *sp)
{
    if (source.megaSurface())
        return source.megaSurface();
    if (source.isAnimated() || source.hasHires())
        return 0;
    if (sp->cpu)
        return sp->cpu;
    if ((int64_t)sp->gl.width * sp->gl.height > 2048 * 1024)
        return 0;
    clearRead(sp);
    return readbackSurface(sp) ? sp->surface : 0;
}

/* Blend src[sourceRect] over dst[destRect]. Both rects have positive size
 * and were already shrunk to their bitmaps by the caller; they are clipped
 * again here so a rounding step can never index outside either surface. */
static void cpuBlit(SDL_Surface *dst, const IntRect &destRect, SDL_Surface *src,
                    const IntRect &sourceRect, int opacity, bool smooth)
{
    const uint32_t *recip = cpuReciprocals();
    const int x0 = std::max(destRect.x, 0), y0 = std::max(destRect.y, 0);
    const int x1 = std::min(destRect.x + destRect.w, dst->w);
    const int y1 = std::min(destRect.y + destRect.h, dst->h);
    if (x1 <= x0 || y1 <= y0 || src->w <= 0 || src->h <= 0)
        return;
    const int sx0 = clamp(sourceRect.x, 0, src->w - 1), sy0 = clamp(sourceRect.y, 0, src->h - 1);
    const int sx1 = clamp(sourceRect.x + sourceRect.w - 1, sx0, src->w - 1);
    const int sy1 = clamp(sourceRect.y + sourceRect.h - 1, sy0, src->h - 1);
    const uint8_t *sp = (const uint8_t *)src->pixels;
    uint8_t *dp = (uint8_t *)dst->pixels;

    if (sourceRect.w == destRect.w && sourceRect.h == destRect.h)
    {
        /* Clip once against the source as well, then run plain rows. */
        const int offX = sourceRect.x - destRect.x, offY = sourceRect.y - destRect.y;
        const int cx0 = std::max(x0, -offX), cx1 = std::min(x1, src->w - offX);
        const int cy0 = std::max(y0, -offY), cy1 = std::min(y1, src->h - offY);
        const int count = cx1 - cx0;
        if (count <= 0)
            return;
        for (int y = cy0; y < cy1; ++y)
        {
            const uint32_t *in = (const uint32_t *)(sp + (size_t)(y + offY) * src->pitch) + cx0 + offX;
            uint32_t *out = (uint32_t *)(dp + (size_t)y * dst->pitch) + cx0;
            if (opacity == 255)
            {
                for (int i = 0; i < count; ++i)
                {
                    const uint32_t s = in[i];
                    const uint32_t sa = s >> 24;
                    if (sa == 0)
                        continue;
                    /* Opaque source, or nothing underneath: a plain copy. */
                    if (sa == 255 || (out[i] >> 24) == 0)
                        out[i] = s;
                    else
                        out[i] = cpuOver(out[i], s, 255, recip);
                }
            }
            else
            {
                for (int i = 0; i < count; ++i)
                    if (in[i] >> 24)
                        out[i] = cpuOver(out[i], in[i], opacity, recip);
            }
        }
        return;
    }

    /* Scaled: map destination pixel centres into the source rect (16.16). */
    const int64_t stepX = ((int64_t)sourceRect.w << 16) / destRect.w;
    const int64_t stepY = ((int64_t)sourceRect.h << 16) / destRect.h;
    for (int y = y0; y < y1; ++y)
    {
        int64_t v = (int64_t)(y - destRect.y) * stepY + stepY / 2 - (smooth ? 0x8000 : 0);
        if (v < 0) v = 0;
        int sy = sourceRect.y + (int)(v >> 16);
        const uint32_t fy = smooth ? (uint32_t)((v & 0xFFFF) >> 8) : 0;
        sy = clamp(sy, sy0, sy1);
        const int syn = std::min(sy + 1, sy1);
        const uint32_t *srow = (const uint32_t *)(sp + (size_t)sy * src->pitch);
        const uint32_t *srown = (const uint32_t *)(sp + (size_t)syn * src->pitch);
        uint32_t *drow = (uint32_t *)(dp + (size_t)y * dst->pitch);
        for (int x = x0; x < x1; ++x)
        {
            int64_t u = (int64_t)(x - destRect.x) * stepX + stepX / 2 - (smooth ? 0x8000 : 0);
            if (u < 0) u = 0;
            int sx = clamp(sourceRect.x + (int)(u >> 16), sx0, sx1);
            uint32_t s;
            if (smooth)
            {
                const uint32_t fx = (uint32_t)((u & 0xFFFF) >> 8);
                const int sxn = std::min(sx + 1, sx1);
                s = cpuLerp(cpuLerp(srow[sx], srow[sxn], fx), cpuLerp(srown[sx], srown[sxn], fx), fy);
            }
            else
                s = srow[sx];
            drow[x] = cpuOver(drow[x], s, opacity, recip);
        }
    }
}

struct BitmapOpenHandler : FileSystem::OpenHandler
{
    // Non-GIF
    SDL_Surface *surface;
    
    // GIF
    std::string error;
    gif_animation *gif;
    unsigned char *gif_data;
    size_t gif_data_size;
    
    
    BitmapOpenHandler()
    : surface(0), gif(0), gif_data(0), gif_data_size(0)
    {}
    
    bool tryRead(SDL_RWops &ops, const char *ext)
    {
        if (IMG_isGIF(&ops)) {
            // Use libnsgif to initialise the gif data
            gif = new gif_animation;
            
            gif_bitmap_callback_vt gif_bitmap_callbacks = {
                gif_bitmap_create,
                gif_bitmap_destroy,
                gif_bitmap_get_buffer,
                gif_bitmap_set_opaque,
                gif_bitmap_test_opaque,
                gif_bitmap_modified
            };
            
            gif_create(gif, &gif_bitmap_callbacks);
            
            gif_data_size = ops.size(&ops);
            
            gif_data = new unsigned char[gif_data_size];
            ops.seek(&ops, 0, RW_SEEK_SET);
            ops.read(&ops, gif_data, gif_data_size, 1);
            
            int status;
            do {
                status = gif_initialise(gif, gif_data_size, gif_data);
                if (status != GIF_OK && status != GIF_WORKING) {
                    gif_finalise(gif);
                    delete gif;
                    delete gif_data;
                    error = "Failed to initialize GIF (Error " + std::to_string(status) + ")";
                    return false;
                }
            } while (status != GIF_OK);
            
            // Decode the first frame
            status = gif_decode_frame(gif, 0);
            if (status != GIF_OK && status != GIF_WORKING) {
                error = "Failed to decode first GIF frame. (Error " + std::to_string(status) + ")";
                gif_finalise(gif);
                delete gif;
                delete gif_data;
                return false;
            }
        } else {
            surface = IMG_LoadTyped_RW(&ops, 1, ext);
        }
#ifdef MKXPZ_VITA_DIAGNOSTICS
        if (!surface && !gif)
        {
            const Sint64 streamSize = ops.size ? ops.size(&ops) : -1;
            unsigned char magic[8] = {0};
            if (ops.seek && ops.seek(&ops, 0, RW_SEEK_SET) == 0 && ops.read)
                ops.read(&ops, magic, 1, sizeof(magic));
            vitaDiagLog("IMGDIAG",
                        "decode_failed ext=%s stream_size=%lld magic=%02x%02x%02x%02x%02x%02x%02x%02x sdl_error=%s",
                        ext ? ext : "<null>", (long long)streamSize,
                        magic[0], magic[1], magic[2], magic[3],
                        magic[4], magic[5], magic[6], magic[7],
                        SDL_GetError());
        }
#endif
        return (surface || gif);
    }
};

Bitmap::Bitmap(const char *filename)
{
    FrameProfile::Scope profile(FrameProfile::BitmapFile);

    std::string hiresPrefix = "Hires/";
    std::string filenameStd = filename;
    Bitmap *hiresBitmap = nullptr;
    // TODO: once C++20 is required, switch to filenameStd.starts_with(hiresPrefix)
    if (shState->config().enableHires && filenameStd.compare(0, hiresPrefix.size(), hiresPrefix) != 0) {
        // Look for a high-res version of the file.
        std::string hiresFilename = hiresPrefix + filenameStd;
        try {
            hiresBitmap = new Bitmap(hiresFilename.c_str());
            hiresBitmap->setLores(this);
        }
        catch (const Exception &e)
        {
            Debug() << "No high-res Bitmap found at" << hiresFilename;
            hiresBitmap = nullptr;
        }
    }

#ifdef __vita__
    /* Prepared on another core from this game's load history. */
    if (!hiresBitmap)
        if (SDL_Surface *ready = AssetPrefetch::takeImage(filename))
        {
            initFromSurface(ready, nullptr, false);
            return;
        }
    const Uint64 loadBegan = SDL_GetPerformanceCounter();
#endif
    BitmapOpenHandler handler;
    try {
        { FrameProfile::Scope decode(FrameProfile::ImageReadDecode); shState->fileSystem().openRead(handler, filename); }
#ifdef __vita__
        if (handler.surface && !hiresBitmap)
        {
            const double ms = double(SDL_GetPerformanceCounter() - loadBegan) * 1000.0 /
                              double(SDL_GetPerformanceFrequency());
            AssetPrefetch::noteImage(filename, ms);
            AssetPrefetch::keepImage(filename, handler.surface, ms);
        }
#endif
        
        if (!handler.error.empty()) {
            // Not loaded with SDL, but I want it to be caught with the same exception type
            throw Exception(Exception::SDLError, "Error loading image '%s': %s", filename, handler.error.c_str());
        }
        else if (!handler.gif && !handler.surface) {
            throw Exception(Exception::SDLError, "Error loading image '%s': %s",
                            filename, SDL_GetError());
        }
    } catch (const Exception &e) {
        if (hiresBitmap)
            delete hiresBitmap;
        throw e;
    }

    /* CONTENT allowlisted: first file-bitmap loads per boot (save-list crash). */
    {
        static int n = 0;
        if (n < 48)
        {
            ++n;
            vitaDiagLog("CONTENT", "bitmap #%d file=%.120s gif=%d %dx%d", n, filename,
                        handler.gif ? 1 : 0,
                        handler.gif ? (int)handler.gif->width : (handler.surface ? handler.surface->w : -1),
                        handler.gif ? (int)handler.gif->height : (handler.surface ? handler.surface->h : -1));
        }
    }

    if (handler.gif) {
        if (handler.gif->width >= (uint32_t)glState.caps.maxTexSize || handler.gif->height > (uint32_t)glState.caps.maxTexSize)
        {
            if (hiresBitmap)
                delete hiresBitmap;
            throw new Exception(Exception::MKXPError, "Animation too large (%ix%i, max %ix%i)",
                                handler.gif->width, handler.gif->height, glState.caps.maxTexSize, glState.caps.maxTexSize);
        }
        
        p = new BitmapPrivate(this);
        
        p->selfHires = hiresBitmap;
        
        if (handler.gif->frame_count == 1) {
            TEXFBO texfbo;
            try {
                texfbo = shState->texPool().request(handler.gif->width, handler.gif->height);
            }
            catch (const Exception &e)
            {
                gif_finalise(handler.gif);
                delete handler.gif;
                delete handler.gif_data;
                
                delete p;
                if (hiresBitmap)
                    delete hiresBitmap;
                
                throw e;
            }
            
            TEXFBO::uploadImage(texfbo, handler.gif->width, handler.gif->height, handler.gif->frame_image, GL_RGBA);
            gif_finalise(handler.gif);
            delete handler.gif;
            delete handler.gif_data;
            
            p->gl = texfbo;
            if (p->selfHires != nullptr) {
                p->gl.selfHires = &p->selfHires->getGLTypes();
            }
            p->addTaintedArea(rect());
            return;
        }
        
        p->animation.enabled = true;
        p->animation.width = handler.gif->width;
        p->animation.height = handler.gif->height;
        
        // Guess framerate based on the first frame's delay
        p->animation.fps = 1 / ((float)handler.gif->frames[handler.gif->decoded_frame].frame_delay / 100);
        if (p->animation.fps < 0) p->animation.fps = shState->graphics().getFrameRate();
        
        // Loop gif (Either it's looping or it's not, at the moment)
        p->animation.loop = handler.gif->loop_count >= 0;
        
        int fcount = handler.gif->frame_count;
        int fcount_partial = handler.gif->frame_count_partial;
        if (fcount > fcount_partial) {
            Debug() << "Non-fatal error reading" << filename << ": Only decoded" << fcount_partial << "out of" << fcount << "frames";
        }
        for (int i = 0; i < fcount_partial; i++) {
            if (i > 0) {
                int status = gif_decode_frame(handler.gif, i);
                if (status != GIF_OK && status != GIF_WORKING) {
                    releaseResources();
                    
                    gif_finalise(handler.gif);
                    delete handler.gif;
                    delete handler.gif_data;
                    
                    throw Exception(Exception::MKXPError, "Failed to decode GIF frame %i out of %i (Status %i)",
                                    i + 1, fcount_partial, status);
                }
            }
            
            TEXFBO texfbo;
            try {
                texfbo = shState->texPool().request(p->animation.width, p->animation.height);
            }
            catch (const Exception &e)
            {
                releaseResources();
                
                gif_finalise(handler.gif);
                delete handler.gif;
                delete handler.gif_data;
                
                throw e;
            }
            
            TEXFBO::uploadImage(texfbo, p->animation.width, p->animation.height, handler.gif->frame_image, GL_RGBA);
            p->animation.frames.push_back(texfbo);
        }
        
        gif_finalise(handler.gif);
        delete handler.gif;
        delete handler.gif_data;
        p->addTaintedArea(rect());
        return;
    }

    SDL_Surface *imgSurf = handler.surface;

    initFromSurface(imgSurf, hiresBitmap, false);
}

Bitmap::Bitmap(int width, int height, bool isHires)
{    SharedState *instWatchEntry = SharedState::instance;
#define INSTWATCH(step) do { \
    SharedState *instWatchNow = SharedState::instance; \
    if (instWatchNow != instWatchEntry) \
        vitaDiagLog("ERROR", "INSTWATCH ctor=%p w=%d h=%d hires=%d step=%s entry=%p now=%p", \
                    (void *)this, width, height, (int)isHires, step, \
                    (void *)instWatchEntry, (void *)instWatchNow); \
} while (0)
    if (!instWatchEntry)
        vitaDiagLog("ERROR", "INSTWATCH ctor=%p w=%d h=%d hires=%d step=%s entry=%p",
                    (void *)this, width, height, (int)isHires, "at_entry", (void *)instWatchEntry);
    vitaDiagLogVglPool("bitmap_ctor");

    if (width <= 0 || height <= 0)
        throw Exception(Exception::RGSSError, "failed to create bitmap");
    
    Bitmap *hiresBitmap = nullptr;

    if (shState->config().enableHires && !isHires) {
        // Create a high-res version as well.
        double scalingFactor = shState->config().textureScalingFactor;
        int hiresWidth = (int)lround(scalingFactor * width);
        int hiresHeight = (int)lround(scalingFactor * height);
        hiresBitmap = new Bitmap(hiresWidth, hiresHeight, true);
        hiresBitmap->setLores(this);
    }

    if (!hiresBitmap && !isHires && cpuBitmapEligible(width, height))
    {
        BitmapPrivate *composed = new BitmapPrivate(this);
        /* SDL_CreateRGBSurface zero-fills: the bitmap starts transparent. */
        composed->cpu = SDL_CreateRGBSurface(0, width, height, composed->format->BitsPerPixel,
                                             composed->format->Rmask, composed->format->Gmask,
                                             composed->format->Bmask, composed->format->Amask);
        if (composed->cpu)
        {
            p = composed;
            cpuBitmapBytes += (size_t)width * height * 4;
            cpuHeapBytes += cpuHeapShare((size_t)width * height * 4);
            p->cpuIter = cpuBitmapList.insert(cpuBitmapList.end(), p);
            p->cpuStamp = cpuFrame;
            p->gl.width = width;
            p->gl.height = height;
            return;
        }
        /* No heap for the pixels: release what can be made again and let
         * the GPU hold this bitmap, as it did before. */
        delete composed;
        dropAllReadbacks();
    }

    TEXFBO tex;
    try {
        tex = shState->texPool().request(width, height);
    } catch (const Exception &e) {
        if (hiresBitmap)
            delete hiresBitmap;
        throw e;
    }
    INSTWATCH("after_texpool");

    p = new BitmapPrivate(this);
    p->gl = tex;
    p->selfHires = hiresBitmap;
    if (p->selfHires != nullptr) {
        p->gl.selfHires = &p->selfHires->getGLTypes();
    }

    INSTWATCH("after_private");
    clear();
#undef INSTWATCH
}

Bitmap::Bitmap(void *pixeldata, int width, int height)
{

    SDL_Surface *surface = SDL_CreateRGBSurface(0, width, height, p->format->BitsPerPixel,
                                                p->format->Rmask,
                                                p->format->Gmask,
                                                p->format->Bmask,
                                                p->format->Amask);
    
    if (!surface)
        throw Exception(Exception::SDLError, "Error creating Bitmap: %s",
                        SDL_GetError());
    
    memcpy(surface->pixels, pixeldata, width*height*(p->format->BitsPerPixel/8));
    
    if (surface->w > glState.caps.maxTexSize || surface->h > glState.caps.maxTexSize)
    {
        p = new BitmapPrivate(this);
        p->megaSurface = surface;
        SDL_SetSurfaceBlendMode(p->megaSurface, SDL_BLENDMODE_NONE);
    }
    else
    {
        TEXFBO tex;
        
        try
        {
            tex = shState->texPool().request(surface->w, surface->h);
            TEXFBO::uploadImage(tex, tex.width, tex.height, surface->pixels, GL_RGBA);
        }
        catch (const Exception &e)
        {
            SDL_FreeSurface(surface);
            throw e;
        }
        
        p = new BitmapPrivate(this);
        vitaDiagLog("BITMAP", "ctor private created w=%d h=%d", width, height);
        p->gl = tex;
        vitaDiagLog("BITMAP", "ctor gl assigned");
        
        vitaDiagLog("BITMAP", "ctor upload done");
        
        SDL_FreeSurface(surface);
    }
    
    p->addTaintedArea(rect());
}

// frame is -2 for "any and all", -1 for "current", anything else for a specific frame
Bitmap::Bitmap(const Bitmap &other, int frame)
{

    other.guardDisposed();
    other.ensureNonMega();
    if (frame > -2) other.ensureAnimated();
    
    if (other.hasHires()) {
        Debug() << "BUG: High-res Bitmap from animation not implemented";
    }

    p = new BitmapPrivate(this);

    if (!other.isAnimated() && other.p->cpu)
    {
        /* A copy of a CPU-composed bitmap is CPU-composed too. */
        SDL_Surface *from = other.p->cpu;
        if (cpuBitmapEligible(from->w, from->h))
            p->cpu = SDL_CreateRGBSurface(0, from->w, from->h, p->format->BitsPerPixel,
                                          p->format->Rmask, p->format->Gmask,
                                          p->format->Bmask, p->format->Amask);
        if (p->cpu)
        {
            for (int y = 0; y < from->h; ++y)
                memcpy((uint8_t *)p->cpu->pixels + (size_t)y * p->cpu->pitch,
                       (const uint8_t *)from->pixels + (size_t)y * from->pitch, (size_t)from->w * 4);
            cpuBitmapBytes += (size_t)from->w * from->h * 4;
            cpuHeapBytes += cpuHeapShare((size_t)from->w * from->h * 4);
            p->cpuIter = cpuBitmapList.insert(cpuBitmapList.end(), p);
            p->cpuStamp = cpuFrame;
            p->gl.width = from->w;
            p->gl.height = from->h;
            p->cpuContent = other.p->cpuContent;
            p->addTaintedArea(rect());
            return;
        }
        /* No room on the CPU side: copy on the GPU, as before. */
        cpuPromote(other.p);
    }
    
    // TODO: Clean me up
    if (!other.isAnimated() || frame >= -1) {
        try {
            p->gl = shState->texPool().request(other.width(), other.height());
        } catch (const Exception &e) {
            delete p;
            throw e;
        }
        
        GLMeta::blitBegin(p->gl, false, SameScale);
        /* Copy pass samples the source bitmap; materialize first. */
        clearRead(other.p);
        // Blit just the current frame of the other animated bitmap
        if (!other.isAnimated() || frame == -1) {
            GLMeta::blitSource(other.getGLTypes(), SameScale);
        }
        else {
            auto &frames = other.getFrames();
            GLMeta::blitSource(frames[clamp(frame, 0, (int)frames.size() - 1)], SameScale);
        }
        GLMeta::blitRectangle(rect(), rect());
        GLMeta::blitEnd();
    }
    else {
        p->animation.enabled = true;
        p->animation.fps = other.getAnimationFPS();
        p->animation.width = other.width();
        p->animation.height = other.height();
        p->animation.lastFrame = 0;
        p->animation.playTime = 0;
        p->animation.startTime = 0;
        p->animation.loop = other.getLooping();
        
        for (TEXFBO &sourceframe : other.getFrames()) {
            TEXFBO newframe;
            try {
                newframe = shState->texPool().request(p->animation.width, p->animation.height);
            } catch(const Exception &e) {
                releaseResources();
                throw e;
            }
            
            GLMeta::blitBegin(newframe, false, SameScale);
            /* Per-frame source sampling; materialize first. */
            clearRead(other.p);
            GLMeta::blitSource(sourceframe, SameScale);
            GLMeta::blitRectangle(rect(), rect());
            GLMeta::blitEnd();
            
            p->animation.frames.push_back(newframe);
        }
    }
    
    p->addTaintedArea(rect());
}

Bitmap::Bitmap(TEXFBO &other)
{

    Bitmap *hiresBitmap = nullptr;

    if (other.selfHires != nullptr) {
        // Create a high-res version as well.
        hiresBitmap = new Bitmap(*other.selfHires);
        hiresBitmap->setLores(this);
    }

    p = new BitmapPrivate(this);
    p->selfHires = hiresBitmap;

    try {
        p->gl = shState->texPool().request(other.width, other.height);
    } catch (const Exception &e) {
        delete p;
        throw e;
    }

    if (p->selfHires != nullptr) {
        p->gl.selfHires = &p->selfHires->getGLTypes();
    }

    // Skip blitting to lores texture, since only the hires one will be displayed.
    if (p->selfHires == nullptr) {
        GLMeta::blitBegin(p->gl, false, SameScale);
        GLMeta::blitSource(other, SameScale);
        GLMeta::blitRectangle(rect(), rect());
        GLMeta::blitEnd();
    }

    p->addTaintedArea(rect());
}

Bitmap::Bitmap(SDL_Surface *imgSurf, SDL_Surface *imgSurfHires, bool forceMega)
{

    Bitmap *hiresBitmap = nullptr;

    if (imgSurfHires != nullptr) {
        // Create a high-res version as well.
        hiresBitmap = new Bitmap(imgSurfHires, nullptr);
        hiresBitmap->setLores(this);
    }

    initFromSurface(imgSurf, hiresBitmap, forceMega);
}

Bitmap::~Bitmap()
{
    vitaDiagLog("BITMAP", "dtor self=%p signal=%p", (void *)this,
                (void *)&wasDisposed);
    const bool diagnosticMega = p && p->megaSurface;
    if (diagnosticMega)
        vitaDiagLog("BITMAP", "mega_destructor_begin self=%p surface=%p",
                    (void *)this, (void *)p->megaSurface);
    dispose();

    if (diagnosticMega)
        vitaDiagLog("BITMAP", "mega_destructor_done self=%p", (void *)this);

    loresDispCon.disconnect();
}

void Bitmap::initFromSurface(SDL_Surface *imgSurf, Bitmap *hiresBitmap, bool forceMega)
{
    p->ensureFormat(imgSurf, SDL_PIXELFORMAT_ABGR8888);
    
    if (imgSurf->w > glState.caps.maxTexSize || imgSurf->h > glState.caps.maxTexSize || forceMega)
    {
        /* Mega surface */

        p = new BitmapPrivate(this);
        p->selfHires = hiresBitmap;
        p->megaSurface = imgSurf;
        SDL_SetSurfaceBlendMode(p->megaSurface, SDL_BLENDMODE_NONE);
    }
    else
    {
        /* Regular surface */
        TEXFBO tex;
        
        try
        {
            tex = shState->texPool().request(imgSurf->w, imgSurf->h);
            TEXFBO::uploadImage(tex, tex.width, tex.height, imgSurf->pixels, GL_RGBA);
        }
        catch (const Exception &e)
        {
            if (hiresBitmap)
                delete hiresBitmap;
            SDL_FreeSurface(imgSurf);
            throw e;
        }
        
        p = new BitmapPrivate(this);
        p->selfHires = hiresBitmap;
        p->gl = tex;
        if (p->selfHires != nullptr) {
            p->gl.selfHires = &p->selfHires->getGLTypes();
        }
        
        SDL_FreeSurface(imgSurf);
    }
    
    p->addTaintedArea(rect());
}

int Bitmap::width() const
{
    guardDisposed();
    
    if (p->megaSurface) {
        return p->megaSurface->w;
    }
    
    if (p->animation.enabled) {
        return p->animation.width;
    }
    
    return p->gl.width;
}

int Bitmap::height() const
{
    guardDisposed();
    
    if (p->megaSurface)
        return p->megaSurface->h;
    
    if (p->animation.enabled)
        return p->animation.height;
    
    return p->gl.height;
}

bool Bitmap::hasHires() const{
    guardDisposed();

    return p->selfHires;
}

DEF_ATTR_RD_SIMPLE(Bitmap, Hires, Bitmap*, p->selfHires)

void Bitmap::setHires(Bitmap *hires) {
    guardDisposed();

    Debug() << "BUG: High-res Bitmap setHires not fully implemented, expect bugs";
    hires->setLores(this);
    p->selfHires = hires;
}

void Bitmap::setLores(Bitmap *lores) {
    guardDisposed();

    p->selfLores = lores;
    loresDispCon = lores->wasDisposed.connect(&Bitmap::loresDisposal, this);

    if (p->font && p->font != &shState->defaultFont())
        p->font->setHiresMult((float)width() / (float)lores->width());
}

bool Bitmap::isMega() const{
    guardDisposed();
    
    return p->megaSurface;
}

bool Bitmap::isAnimated() const {
    guardDisposed();
    
    return p->animation.enabled;
}

IntRect Bitmap::rect() const
{
    guardDisposed();
    
    return IntRect(0, 0, width(), height());
}

void Bitmap::blt(int x, int y,
                 const Bitmap &source, const IntRect &rect,
                 int opacity)
{
    if (source.isDisposed())
        return;
    
    stretchBlt(IntRect(x, y, abs(rect.w), abs(rect.h)),
               source, rect, opacity);
}

static bool shrinkRects(float &sourcePos, float &sourceLen, const int &sBitmapLen,
                         float &destPos, float &destLen, const int &dBitmapLen, bool normalize = false)
{
    float sStart = sourceLen > 0 ? sourcePos : sourceLen + sourcePos;
    float sEnd = sourceLen > 0 ? sourceLen + sourcePos : sourcePos;
    float sLength = sEnd - sStart;
    
    if (sStart >= 0 && sEnd < sBitmapLen)
        return false;
    
    if (sStart >= sBitmapLen || sEnd < 0)
        return true;
    
    float dStart = destLen > 0 ? destPos: destLen + destPos;
    float dEnd = destLen > 0 ? destLen + destPos : destPos;
    float dLength = dEnd - dStart;
    
    float delta = sEnd - sBitmapLen;
    float dDelta;
    if (delta > 0)
    {
        dDelta = (delta / sLength) * dLength;
        sLength -= delta;
        sEnd = sBitmapLen;
        dEnd -= dDelta;
        dLength -= dDelta;
    }
    if (sStart < 0)
    {
        dDelta = (sStart / sLength) * dLength;
        sLength += sStart;
        sStart = 0;
        dStart -= dDelta;
        dLength += dDelta;
    }
    
    if (!normalize)
    {
        sourcePos = sourceLen > 0 ? sStart : sEnd;
        sourceLen = sourceLen > 0 ? sLength : -sLength;
        destPos = destLen > 0  ? dStart : dEnd;
        destLen = destLen > 0 ? dLength : -dLength;
    }
    else
    {
        // Ensure the source rect has positive dimensions, for blitting from mega surfaces
        destPos = (destLen > 0 == sourceLen > 0) ? dStart : dEnd;
        destLen = (destLen > 0 == sourceLen > 0) ? dLength : -dLength;
        sourcePos = sStart;
        sourceLen = sLength;
    }
    
    return false;
}

static bool shrinkRects(int &sourcePos, int &sourceLen, const int &sBitmapLen,
                         int &destPos, int &destLen, const int &dBitmapLen)
{
    float fSourcePos = sourcePos;
    float fSourceLen = sourceLen;
    float fDestPos = destPos;
    float fDestLen = destLen;
    
    bool ret = shrinkRects(fSourcePos, fSourceLen, sBitmapLen, fDestPos, fDestLen, dBitmapLen, true);
    
    if (!ret)
        ret = shrinkRects(fDestPos, fDestLen, dBitmapLen, fSourcePos, fSourceLen, sBitmapLen);
    
    sourcePos = round(fSourcePos);
    sourceLen = round(fSourceLen);
    destPos = round(fDestPos);
    destLen = round(fDestLen);
    
    return ret || sourceLen == 0 || destLen == 0;
}

static float bltNormOpacity(enum Bitmap::BitmapBltMode mode, int opacity)
{
    opacity = clamp(opacity, 0, 255);

    switch (mode)
    {
        case Bitmap::NORMAL:
            return (float)opacity / 255.0f;

        case Bitmap::KGL_SUBTRACT:
            return opacity >= 255 ? 1.0f : (float)opacity / 256.0f;
    }

    /* Unknown modes use normal opacity normalization. */
    return (float)opacity / 255.0f;
}

static void bltFilter(enum Bitmap::BitmapBltMode mode, uint32_t &dst_pixel, uint32_t src_pixel, float norm_opacity)
{
    switch (mode)
    {
        case Bitmap::NORMAL:
            __builtin_unreachable();
            break;

        case Bitmap::KGL_SUBTRACT:
            for (size_t i = 0; i < 3; ++i)
            {
                uint8_t &old_component = ((uint8_t *)&dst_pixel)[i];
                uint8_t old_component_with_opacity = (uint8_t)std::round(norm_opacity * (float)old_component);
                uint8_t new_component = ((uint8_t *)&src_pixel)[i];
                old_component = new_component > old_component_with_opacity ? new_component - old_component_with_opacity : 0;
            }
            ((uint8_t *)&dst_pixel)[3] = 255;
            break;
    }
}

void Bitmap::stretchBlt(IntRect destRect,
                        const Bitmap &source, IntRect sourceRect,
                        int opacity, bool smooth,
                        enum BitmapBltMode mode)
{
    FrameProfile::Scope profile(FrameProfile::BlitPrepare);
    guardDisposed();

    // Don't need this, right? This function is fine with megasurfaces it seems
    //GUARD_MEGA;

    /* CONTENT allowlisted: first blts per boot (save-list crash site). */
    {
        static int n = 0;
        if (n < 48)
        {
            ++n;
            vitaDiagLog("CONTENT", "blt #%d dst=%dx%d+%d+%d src=%dx%d+%d+%d op=%d",
                        n, width(), height(), destRect.x, destRect.y,
                        source.width(), source.height(), sourceRect.x, sourceRect.y,
                        opacity);
        }
    }

    if (source.isDisposed())
        return;

    if (hasHires()) {
        int destX, destY, destWidth, destHeight;
        destX = destRect.x * p->selfHires->width() / width();
        destY = destRect.y * p->selfHires->height() / height();
        destWidth = destRect.w * p->selfHires->width() / width();
        destHeight = destRect.h * p->selfHires->height() / height();

        p->selfHires->stretchBlt(IntRect(destX, destY, destWidth, destHeight), source, sourceRect, opacity);
        return;
    }

    if (source.hasHires()) {
        int sourceX, sourceY, sourceWidth, sourceHeight;
        sourceX = sourceRect.x * source.getHires()->width() / source.width();
        sourceY = sourceRect.y * source.getHires()->height() / source.height();
        sourceWidth = sourceRect.w * source.getHires()->width() / source.width();
        sourceHeight = sourceRect.h * source.getHires()->height() / source.height();

        stretchBlt(destRect, *source.getHires(), IntRect(sourceX, sourceY, sourceWidth, sourceHeight), opacity);
        return;
    }

    opacity = clamp(opacity, 0, 255);
    
    if (opacity == 0)
        switch (mode) {
            case NORMAL:
                return;
            case KGL_SUBTRACT:
                break;
        }
    
    float normOpacity = bltNormOpacity(mode, opacity);
    
    if(shrinkRects(sourceRect.x, sourceRect.w, source.width(), destRect.x, destRect.w, width()))
        return;
    if(shrinkRects(sourceRect.y, sourceRect.h, source.height(), destRect.y, destRect.h, height()))
        return;
    
    if (p->cpu)
    {
        SDL_Surface *pixels = 0;
        if (mode == NORMAL && source.p != p && sourceRect.w > 0 && sourceRect.h > 0 &&
            destRect.w > 0 && destRect.h > 0)
            pixels = cpuSourcePixels(source, source.p);
        if (pixels)
        {
            cpuBlit(p->cpu, destRect, pixels, sourceRect, opacity,
                    smooth && !(sourceRect.w == destRect.w && sourceRect.h == destRect.h));
            const IntRect changed = cpuClip(destRect, p->cpu->w, p->cpu->h);
            p->addTaintedArea(changed);
            cpuTouch(p, changed);
            p->onModified();
            return;
        }
        cpuPromote(p);
    }

    SDL_Surface *srcSurf = source.megaSurface();
    /* A CPU-composed source drawn into a GPU bitmap is uploaded from its
     * pixels, like text and mega surfaces, and stays CPU-composed. */
    if (!srcSurf && !source.isAnimated() && source.p->cpu)
        srcSurf = source.p->cpu;
    SDL_Surface *blitTemp = 0;
    /* Source pixels are observed; dest is overwritten by the blit below,
     * so the pending clear fuses with it. Source first so a self-blit
     * materializes conservatively as a read. */
    clearRead(source.p);
    /* Sub-rect blit below must observe pending pixels everywhere it does
     * not opaquely overwrite. Conservative choice: materialize whenever
     * the blit is not provably an opaque full-bitmap overwrite. The
     * tainted-area fast path only skips blending for untainted (already
     * clear) regions, so a pending clear must be materialized first even
     * for opaque blits: otherwise the copy pass blends source over stale
     * storage instead of clear. */
    clearRead(p);
    bool touchesTaintedArea = mode != NORMAL || opacity < 255 || p->touchesTaintedArea(destRect);
    bool unpack_subimage = srcSurf && gl.unpack_subimage;

    const bool scaleIsOne = sourceRect.w == destRect.w && sourceRect.h == destRect.h;
    if (scaleIsOne) {
        smooth = false;
    }

    if (!srcSurf && !touchesTaintedArea)
    {
        FrameProfile::Scope submit(FrameProfile::BlitSubmit);
        /* Fast blit */
        // TODO: Use bitmapSmoothScaling/bitmapSmoothScalingDown configs for this.
        GLMeta::blitBegin(getGLTypes());
        GLMeta::blitSource(source.getGLTypes());
        GLMeta::blitRectangle(sourceRect, destRect, smooth);
        GLMeta::blitEnd();
    }
    else
    {
        if (srcSurf)
        {
            SDL_Rect srcRect = sourceRect;
            bool subImageFix = shState->config().subImageFix;
            bool srcRectTooBig = srcRect.w > glState.caps.maxTexSize ||
                                 srcRect.h > glState.caps.maxTexSize;
            bool srcSurfTooBig = !unpack_subimage && (
                                     srcSurf->w > glState.caps.maxTexSize || 
                                     srcSurf->h > glState.caps.maxTexSize
                                 );
            
            if (srcRectTooBig || srcSurfTooBig)
            {
                int error;
                if (srcRectTooBig)
                {
                    /* We have to resize it here anyway, so use software resizing */
                    blitTemp =
                        SDL_CreateRGBSurface(0, abs(destRect.w), abs(destRect.h), p->format->BitsPerPixel,
                                             p->format->Rmask, p->format->Gmask,
                                             p->format->Bmask, p->format->Amask);
                    if (!blitTemp)
                        throw Exception(Exception::SDLError, "Error creating temporary surface for blitting: %s",
                                        SDL_GetError());
                    
                    if (smooth)
                    {
                        if (mode == NORMAL)
                            error = SDL_SoftStretchLinear(srcSurf, &srcRect, blitTemp, 0);
                        else
                        {

                            double w_ratio = (double)srcRect.w / (double)destRect.w;
                            double h_ratio = (double)srcRect.h / (double)destRect.h;
                            for (size_t r = 0; r < (size_t)blitTemp->h; ++r)
                                for (size_t c = 0; c < (size_t)blitTemp->w; ++c)
                                {
                                    size_t src_c0 = (size_t)std::floor(w_ratio * c);
                                    size_t src_r0 = (size_t)std::floor(h_ratio * r);
                                    double src_w0 = w_ratio * c - src_c0;
                                    double src_h0 = h_ratio * r - src_r0;
                                    double src_00 = ((uint32_t *)srcSurf->pixels)[(size_t)srcSurf->w * ((size_t)srcRect.y + src_r0) + ((size_t)srcRect.x + src_c0)];
                                    double src_01 = src_c0 + 1 >= (size_t)srcRect.w
                                        ? src_00
                                        : ((uint32_t *)srcSurf->pixels)[(size_t)srcSurf->w * ((size_t)srcRect.y + src_r0) + ((size_t)srcRect.x + src_c0 + 1)];
                                    double src_10 = src_r0 + 1 >= (size_t)srcRect.h
                                        ? src_00
                                        : ((uint32_t *)srcSurf->pixels)[(size_t)srcSurf->w * ((size_t)srcRect.y + src_r0 + 1) + ((size_t)srcRect.x + src_c0)];
                                    double src_11 = src_c0 + 1 >= (size_t)srcRect.w
                                        ? src_10
                                        : (size_t)src_r0 + 1 >= (size_t)srcRect.h
                                        ? src_01
                                        : ((uint32_t *)srcSurf->pixels)[(size_t)srcSurf->w * ((size_t)srcRect.y + src_r0 + 1) + ((size_t)srcRect.x + src_c0 + 1)];
                                    uint32_t &dst_pixel = ((uint32_t *)blitTemp->pixels)[(size_t)blitTemp->w * r + c];
                                    uint32_t src_pixel = std::round(
                                        src_00 * (1. - src_w0) * (1. - src_h0)
                                            + src_01 * (1. - src_w0) * src_h0
                                            + src_10 * src_w0 * (1. - src_h0)
                                            + src_11 * src_w0 * src_h0
                                    );
                                    bltFilter(mode, dst_pixel, src_pixel, normOpacity);
                                }
                        }
                        smooth = false;
                    }
                    else if (mode == NORMAL)
                    {
                        SDL_Rect tmpRect = {0, 0, blitTemp->w, blitTemp->h};
                        error = SDL_LowerBlitScaled(srcSurf, &srcRect, blitTemp, &tmpRect);
                    }
                    else
                    {
                        double w_ratio = (double)srcRect.w / (double)destRect.w;
                        double h_ratio = (double)srcRect.h / (double)destRect.h;
                        for (size_t r = 0; r < (size_t)blitTemp->h; ++r)
                            for (size_t c = 0; c < (size_t)blitTemp->w; ++c)
                            {
                                uint32_t &dst_pixel = ((uint32_t *)blitTemp->pixels)[(size_t)blitTemp->w * r + c];
                                uint32_t src_pixel = ((uint32_t *)srcSurf->pixels)[(size_t)srcSurf->w * ((size_t)srcRect.y + (size_t)std::round(h_ratio * r)) + ((size_t)srcRect.x + (size_t)std::round(w_ratio * c))];
                                bltFilter(mode, dst_pixel, src_pixel, normOpacity);
                            }
                    }
                    unpack_subimage = false;
                }
                else
                {
                    /* Just crop it, let the shader resize it later */
                    blitTemp =
                        SDL_CreateRGBSurface(0, sourceRect.w, sourceRect.h, p->format->BitsPerPixel,
                                             p->format->Rmask, p->format->Gmask,
                                             p->format->Bmask, p->format->Amask);
                    if (!blitTemp)
                        throw Exception(Exception::SDLError, "Error creating temporary surface for blitting: %s",
                                        SDL_GetError());
                    
                    SDL_Rect tmpRect = {0, 0, blitTemp->w, blitTemp->h};
                    error = SDL_LowerBlit(srcSurf, &srcRect, blitTemp, &tmpRect);
                }
                
                if (error)
                {
                    SDL_FreeSurface(blitTemp);
                    throw Exception(Exception::SDLError, "Failed to blit surface: %s", SDL_GetError());
                }
                
                srcSurf = blitTemp;
                
                sourceRect.w = srcSurf->w;
                sourceRect.h = srcSurf->h;
                sourceRect.x = 0;
                sourceRect.y = 0;
            }
            
            if (!touchesTaintedArea)
            {
                if (!subImageFix &&
                    sourceRect.w == destRect.w && sourceRect.h == destRect.h &&
                    (unpack_subimage || (srcSurf->w == sourceRect.w && srcSurf->h == sourceRect.h))
                   )
                {
                    /* No scaling needed */
                    TEX::bind(getGLTypes().tex);
                    if (unpack_subimage)
                    {
                        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, srcSurf->w);
                        gl.PixelStorei(GL_UNPACK_SKIP_PIXELS, sourceRect.x);
                        gl.PixelStorei(GL_UNPACK_SKIP_ROWS, sourceRect.y);
                    }
                    TEX::uploadSubImage(destRect.x, destRect.y,
                                        destRect.w, destRect.h,
                                        srcSurf->pixels, GL_RGBA);
                    relinkAfterUpload(getGLTypes());
                    vitaDiagLog("DEBUG-aoni", "direct-upload tex=%dx%d rect=%d,%d %dx%d upbar=%u upbar_us=%llu",
                                p->gl.width, p->gl.height,
                                destRect.x, destRect.y, destRect.w, destRect.h,
#ifdef __vita__
                                vgl_upload_barrier_count, vgl_upload_barrier_us
#else
                                0u, 0ull
#endif
                                );
                    
                    if (unpack_subimage)
                        GLMeta::subRectImageEnd();
                }
                else
                {
                    /* Resizing or subImageFix involved: need to use intermediary TexFBO */
                    TEXFBO *gpTF;
#ifdef __vita__
                    if (!unpack_subimage)
                        gpTF = &shState->uploadScratchFBO(srcSurf->w, srcSurf->h, srcSurf->pixels);
                    else
#endif
                    {
                        if (unpack_subimage)
                            gpTF = &shState->gpTexFBO(sourceRect.w, sourceRect.h);
                        else
                            gpTF = &shState->gpTexFBO(srcSurf->w, srcSurf->h);
                        TEX::bind(gpTF->tex);

                        if (unpack_subimage)
                        {
                            gl.PixelStorei(GL_UNPACK_ROW_LENGTH, srcSurf->w);
                            gl.PixelStorei(GL_UNPACK_SKIP_PIXELS, sourceRect.x);
                            gl.PixelStorei(GL_UNPACK_SKIP_ROWS, sourceRect.y);
                            sourceRect.x = 0;
                            sourceRect.y = 0;
                            TEX::uploadSubImage(0, 0, sourceRect.w, sourceRect.h, srcSurf->pixels, GL_RGBA);
                            GLMeta::subRectImageEnd();
                        }
                        else
                        {
                            TEX::uploadSubImage(0, 0, srcSurf->w, srcSurf->h, srcSurf->pixels, GL_RGBA);
                        }

                    }
                    GLMeta::blitBegin(getGLTypes());
                    GLMeta::blitSource(*gpTF);
                    GLMeta::blitRectangle(sourceRect, destRect, smooth);
                    GLMeta::blitEnd();
                }
            }
        }
        if (touchesTaintedArea)
        {
            /* We're touching a tainted area or still need to reduce opacity */

            /* Fragment pipeline */

            TEXFBO &gpTex = shState->gpTexFBO(abs(destRect.w), abs(destRect.h));
            Vec2i gpTexSize;

            { FrameProfile::Scope copy(FrameProfile::DestinationCopy);
            /* Dest pixels are read into the scratch copy; materialize first.
             * Note: the materialized clear runs on this bitmap's own FBO,
             * then this binds gpTex, so it does not fuse the later blit. */
            clearRead(p);
            GLMeta::blitBegin(gpTex, false, SameScale);
            GLMeta::blitSource(getGLTypes(), SameScale);
            GLMeta::blitRectangle(destRect, IntRect(0, 0, abs(destRect.w), abs(destRect.h)));
            GLMeta::blitEnd();

            }
            int sourceWidth, sourceHeight;
            FloatRect bltSubRect;
            if (srcSurf)
            {
#ifdef __vita__
                if (!unpack_subimage)
                {
                    shState->uploadScratchTexture(srcSurf->w, srcSurf->h,
                                                  srcSurf->pixels, gpTexSize);
                    sourceWidth = gpTexSize.x;
                    sourceHeight = gpTexSize.y;
                }
                else
#endif
                {
                    if (unpack_subimage)
                    {
                        shState->ensureTexSize(sourceRect.w, sourceRect.h, gpTexSize);
                    }
                    else
                    {
                        shState->ensureTexSize(srcSurf->w, srcSurf->h, gpTexSize);
                    }
                    sourceWidth = gpTexSize.x;
                    sourceHeight = gpTexSize.y;

                    shState->bindTex();

                    if (unpack_subimage)
                    {
                        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, srcSurf->w);
                        gl.PixelStorei(GL_UNPACK_SKIP_PIXELS, sourceRect.x);
                        gl.PixelStorei(GL_UNPACK_SKIP_ROWS, sourceRect.y);
                        sourceRect.x = 0;
                        sourceRect.y = 0;

                        TEX::uploadSubImage(0, 0, sourceRect.w, sourceRect.h, srcSurf->pixels, GL_RGBA);
                        GLMeta::subRectImageEnd();
                    }
                    else
                    {
                        TEX::uploadSubImage(0, 0, srcSurf->w, srcSurf->h, srcSurf->pixels, GL_RGBA);
                    }
                }
            }
            else
            {
                sourceWidth = source.width();
                sourceHeight = source.height();
            }
            FrameProfile::Scope submit(FrameProfile::BlitSubmit);
            bltSubRect = FloatRect((float) sourceRect.x / sourceWidth,
                                   (float) sourceRect.y / sourceHeight,
                                   ((float) sourceWidth / sourceRect.w) * ((float) abs(destRect.w) / gpTex.width),
                                   ((float) sourceHeight / sourceRect.h) * ((float) abs(destRect.h) / gpTex.height));
            
            BltShader &shader = mode == KGL_SUBTRACT
                ? static_cast<KglSubtractShader &>(shState->shaders().kglSubtract)
                : static_cast<BltShader &>(shState->shaders().blt);
            shader.bind();
            if (srcSurf)
            {
                shader.setTexSize(gpTexSize);
            }
            else
            {
                source.p->bindTexture(shader, false);
            }
            shader.setSource();
            shader.setDestination(gpTex.tex);
            shader.setSubRect(bltSubRect);
            shader.setOpacity(normOpacity);
            
            Quad &quad = shState->gpQuad();
            quad.setTexPosRect(sourceRect, destRect);
            quad.setColor(Vec4(1, 1, 1, normOpacity));
            
            p->bindFBO();
            p->pushSetViewport(shader);
            
            if (smooth)
                TEX::setSmooth(true);
            
            p->blitQuad(quad);
            
            p->popViewport();
            
            if (smooth)
                TEX::setSmooth(false);
        }
    }
    
    if (blitTemp)
        SDL_FreeSurface(blitTemp);
    
    p->addTaintedArea(destRect);
    p->onModified();
}

void Bitmap::fillRect(int x, int y,
                      int width, int height,
                      const Vec4 &color)
{
    fillRect(IntRect(x, y, width, height), color);
}

void Bitmap::fillRect(const IntRect &rect, const Vec4 &color)
{
    guardDisposed();
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    
    if (hasHires()) {
        int destX, destY, destWidth, destHeight;
        destX = rect.x * p->selfHires->width() / width();
        destY = rect.y * p->selfHires->height() / height();
        destWidth = rect.w * p->selfHires->width() / width();
        destHeight = rect.h * p->selfHires->height() / height();

        p->selfHires->fillRect(IntRect(destX, destY, destWidth, destHeight), color);
    }

    if (p->cpu)
    {
        const IntRect clip = cpuClip(rect, p->cpu->w, p->cpu->h);
        cpuFill(p->cpu, clip, cpuPack(color));
        if (color.w == 0)
            p->substractTaintedArea(clip);
        else
            p->addTaintedArea(clip);
        cpuTouch(p, clip);
        p->onModified();
        return;
    }

    /* Clear untouched pixels before painting this partial rectangle. */
    { FrameProfile::Scope partial(FrameProfile::ClearPartial); }
    clearBeforePartialWrite(p);

    p->fillRect(rect, color);
    
    if (color.w == 0)
    /* Clear op */
        p->substractTaintedArea(rect);
    else
    /* Fill op */
        p->addTaintedArea(rect);
    
    p->onModified();
}

void Bitmap::gradientFillRect(int x, int y,
                              int width, int height,
                              const Vec4 &color1, const Vec4 &color2,
                              bool vertical)
{
    gradientFillRect(IntRect(x, y, width, height), color1, color2, vertical);
}

void Bitmap::gradientFillRect(const IntRect &rect,
                              const Vec4 &color1, const Vec4 &color2,
                              bool vertical)
{
    guardDisposed();
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    
    if (hasHires()) {
        int destX, destY, destWidth, destHeight;
        destX = rect.x * p->selfHires->width() / width();
        destY = rect.y * p->selfHires->height() / height();
        destWidth = rect.w * p->selfHires->width() / width();
        destHeight = rect.h * p->selfHires->height() / height();

        p->selfHires->gradientFillRect(IntRect(destX, destY, destWidth, destHeight), color1, color2, vertical);
    }

    if (p->cpu)
    {
        const IntRect whole = normalizedRect(rect);
        const IntRect clip = cpuClip(rect, p->cpu->w, p->cpu->h);
        if (clip.w > 0 && clip.h > 0)
            cpuGradient(p->cpu, whole, clip, color1, color2, vertical);
        p->addTaintedArea(clip);
        cpuTouch(p, clip);
        p->onModified();
        return;
    }

    /* Clear untouched pixels before painting this partial gradient. */
    clearBeforePartialWrite(p);

    SimpleColorShader &shader = shState->shaders().simpleColor;
    shader.bind();
    shader.setTranslation(Vec2i());
    
    Quad &quad = shState->gpQuad();
    
    if (vertical)
    {
        quad.vert[0].color = color1;
        quad.vert[1].color = color1;
        quad.vert[2].color = color2;
        quad.vert[3].color = color2;
    }
    else
    {
        quad.vert[0].color = color1;
        quad.vert[3].color = color1;
        quad.vert[1].color = color2;
        quad.vert[2].color = color2;
    }
    
    quad.setPosRect(rect);
    
    p->bindFBO();
    p->pushSetViewport(shader);
    
    p->blitQuad(quad);
    
    p->popViewport();
    
    p->addTaintedArea(rect);
    
    p->onModified();
}

void Bitmap::clearRect(int x, int y, int width, int height)
{
    clearRect(IntRect(x, y, width, height));
}

void Bitmap::clearRect(const IntRect &rect)
{
    guardDisposed();
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    
    if (hasHires()) {
        int destX, destY, destWidth, destHeight;
        destX = rect.x * p->selfHires->width() / width();
        destY = rect.y * p->selfHires->height() / height();
        destWidth = rect.w * p->selfHires->width() / width();
        destHeight = rect.h * p->selfHires->height() / height();

        p->selfHires->clearRect(IntRect(destX, destY, destWidth, destHeight));
    }

    if (p->cpu)
    {
        const IntRect clip = cpuClip(rect, p->cpu->w, p->cpu->h);
        cpuFill(p->cpu, clip, 0);
        cpuMarkDirty(p, clip);
        p->onModified();
        return;
    }

    /* The partial scissored clear cannot replace a pending full clear. */
    { FrameProfile::Scope partial(FrameProfile::ClearPartial); }
    clearBeforePartialWrite(p);

    p->fillRect(rect, Vec4());
    
    p->onModified();
}

void Bitmap::blur()
{
    guardDisposed();
    /* No CPU version of this operation. */
    cpuPromote(p);
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    
    if (hasHires()) {
        p->selfHires->blur();
    }

    /* Blur samples existing pixels; materialize before the filter pass. */
    clearRead(p);

    // TODO: Is there some kind of blur radius that we need to handle for high-res mode?

    Quad &quad = shState->gpQuad();
    FloatRect rect(0, 0, width(), height());
    quad.setTexPosRect(rect, rect);
    
    TEXFBO auxTex = shState->texPool().request(width(), height());
    
    BlurShader &shader = shState->shaders().blur;
    BlurShader::HPass &pass1 = shader.pass1;
    BlurShader::VPass &pass2 = shader.pass2;
    
    glState.blend.pushSet(false);
    glState.viewport.pushSet(IntRect(0, 0, width(), height()));
    
    TEX::bind(p->gl.tex);
    FBO::bind(auxTex.fbo);
    
    pass1.bind();
    pass1.setTexSize(Vec2i(width(), height()));
    pass1.applyViewportProj();
    
    quad.draw();
    
    TEX::bind(auxTex.tex);
    p->bindFBO();
    
    pass2.bind();
    pass2.setTexSize(Vec2i(width(), height()));
    pass2.applyViewportProj();
    
    quad.draw();
    
    glState.viewport.pop();
    glState.blend.pop();
    
    shState->texPool().release(auxTex);
    
    p->onModified();
}

void Bitmap::radialBlur(int angle, int divisions)
{
    guardDisposed();
    /* No CPU version of this operation. */
    cpuPromote(p);
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    
    if (hasHires()) {
        p->selfHires->radialBlur(angle, divisions);
        return;
    }

    /* Radial blur samples existing pixels; materialize first. */
    clearRead(p);

    angle     = clamp<int>(angle, 0, 359);
    divisions = clamp<int>(divisions, 2, 100);
    
    const int _width = width();
    const int _height = height();
    
    float angleStep = (float) angle / (divisions-1);
    float opacity   = 1.0f / divisions;
    float baseAngle = -((float) angle / 2);
    
    ColorQuadArray qArray;
    qArray.resize(5);
    
    std::vector<Vertex> &vert = qArray.vertices;
    
    int i = 0;
    
    /* Center */
    FloatRect texRect(0, 0, _width, _height);
    FloatRect posRect(0, 0, _width, _height);
    
    i += Quad::setTexPosRect(&vert[i*4], texRect, posRect);
    
    /* Upper */
    posRect = FloatRect(0, 0, _width, -_height);
    
    i += Quad::setTexPosRect(&vert[i*4], texRect, posRect);
    
    /* Lower */
    posRect = FloatRect(0, _height*2, _width, -_height);
    
    i += Quad::setTexPosRect(&vert[i*4], texRect, posRect);
    
    /* Left */
    posRect = FloatRect(0, 0, -_width, _height);
    
    i += Quad::setTexPosRect(&vert[i*4], texRect, posRect);
    
    /* Right */
    posRect = FloatRect(_width*2, 0, -_width, _height);
    
    i += Quad::setTexPosRect(&vert[i*4], texRect, posRect);
    
    for (int i = 0; i < 4*5; ++i)
        vert[i].color = Vec4(1, 1, 1, opacity);
    
    qArray.commit();
    
    TEXFBO newTex = shState->texPool().request(_width, _height);
    
    FBO::bind(newTex.fbo);
    
    glState.clearColor.pushSet(Vec4());
    FBO::clear();
    
    Transform trans;
    trans.setOrigin(Vec2(_width / 2.0f, _height / 2.0f));
    trans.setPosition(Vec2(_width / 2.0f, _height / 2.0f));
    
    glState.blendMode.pushSet(BlendAddition);
    
    SimpleMatrixShader &shader = shState->shaders().simpleMatrix;
    shader.bind();
    
    p->bindTexture(shader, false);
    TEX::setSmooth(true);
    
    p->pushSetViewport(shader);
    
    for (int i = 0; i < divisions; ++i)
    {
        trans.setRotation(baseAngle + i*angleStep);
        shader.setMatrix(trans.getMatrix());
        qArray.draw();
    }
    
    p->popViewport();
    
    TEX::setSmooth(false);
    
    glState.blendMode.pop();
    glState.clearColor.pop();
    
    shState->texPool().release(p->gl);
    p->gl = newTex;
    
    p->onModified();
}

void Bitmap::clear()
{
    guardDisposed();
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    
    if (hasHires()) {
        p->selfHires->clear();
    }

    if (p->cpu)
    {
        /* Only what was drawn can be visible; erase and re-upload just that. */
        const IntRect drawn = p->cpuContent;
        if (drawn.w > 0 && drawn.h > 0)
        {
            cpuFill(p->cpu, drawn, 0);
            cpuMarkDirty(p, drawn);
        }
        p->cpuContent = IntRect(0, 0, 0, 0);
        p->clearTaintedArea();
        p->onModified();
        return;
    }

    /* Deferred: record intent only. Tainted bookkeeping and modification
     * notification stay immediate so dimensions, disposal checks and
     * observers behave as before; no GPU work happens here. */
    p->clearTaintedArea();
    p->onModified();
    clearArm(p);

}

static uint32_t &getPixelAt(SDL_Surface *surf, SDL_PixelFormat *form, int x, int y)
{
    size_t offset = x*form->BytesPerPixel + y*surf->pitch;
    uint8_t *bytes = (uint8_t*) surf->pixels + offset;
    
    return *((uint32_t*) bytes);
}

Color Bitmap::getPixel(int x, int y) const
{
    guardDisposed();
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    
    if (hasHires()) {
        Debug() << "GAME BUG: Game is calling getPixel on low-res Bitmap; you may want to patch the game to improve graphics quality.";

        int xHires = x * p->selfHires->width() / width();
        int yHires = y * p->selfHires->height() / height();

        // We take the average color from the high-res Bitmap.
        // RGB channels skip fully transparent pixels when averaging.
        int w = p->selfHires->width() / width();
        int h = p->selfHires->height() / height();

        if (w >= 1 && h >= 1) {
            double rSum = 0.;
            double gSum = 0.;
            double bSum = 0.;
            double aSum = 0.;

            long long rgbCount = 0;
            long long aCount = 0;

            for (int thisX = xHires; thisX < xHires+w && thisX < p->selfHires->width(); thisX++) {
                for (int thisY = yHires; thisY < yHires+h && thisY < p->selfHires->height(); thisY++) {
                    Color thisColor = p->selfHires->getPixel(thisX, thisY);
                    if (thisColor.getAlpha() >= 1.0) {
                        rSum += thisColor.getRed();
                        gSum += thisColor.getGreen();
                        bSum += thisColor.getBlue();
                        rgbCount++;
                    }
                    aSum += thisColor.getAlpha();
                    aCount++;
                }
            }

            double rAvg = rSum / (double)rgbCount;
            double gAvg = gSum / (double)rgbCount;
            double bAvg = bSum / (double)rgbCount;
            double aAvg = aSum / (double)aCount;

            return Color(rAvg, gAvg, bAvg, aAvg);
        }
    }

    if (x < 0 || y < 0 || x >= width() || y >= height())
        return Vec4();

    if (p->cpu)
    {
        const uint32_t pixel = getPixelAt(p->cpu, p->format, x, y);
        return Color((pixel >> p->format->Rshift) & 0xFF,
                     (pixel >> p->format->Gshift) & 0xFF,
                     (pixel >> p->format->Bshift) & 0xFF,
                     (pixel >> p->format->Ashift) & 0xFF);
    }

    /* Pixel observation; materialize a pending clear first. */
    clearRead(p);

    if (!readbackSurface(p))
        throw Exception(Exception::MKXPError, "Bitmap#get_pixel: out of memory copying %dx%d pixels",
                        p->gl.width, p->gl.height);

    uint32_t pixel = getPixelAt(p->surface, p->format, x, y);
    
    return Color((pixel >> p->format->Rshift) & 0xFF,
                 (pixel >> p->format->Gshift) & 0xFF,
                 (pixel >> p->format->Bshift) & 0xFF,
                 (pixel >> p->format->Ashift) & 0xFF);
}

void Bitmap::setPixel(int x, int y, const Color &color)
{
    guardDisposed();
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    /* Single-pixel write follows a pending full clear; materialize first
     * so the untouched pixels stay clear, then the upload overwrites one. */
    clearBeforePartialWrite(p);
    if (hasHires()) {
        Debug() << "GAME BUG: Game is calling setPixel on low-res Bitmap; you may want to patch the game to improve graphics quality.";

        int xHires = x * p->selfHires->width() / width();
        int yHires = y * p->selfHires->height() / height();

        int w = p->selfHires->width() / width();
        int h = p->selfHires->height() / height();

        if (w >= 1 && h >= 1) {
            for (int thisX = xHires; thisX < xHires+w && thisX < p->selfHires->width(); thisX++) {
                for (int thisY = yHires; thisY < yHires+h && thisY < p->selfHires->height(); thisY++) {
                    p->selfHires->setPixel(thisX, thisY, color);
                }
            }
        }
    }

    uint8_t pixel[] =
    {
        (uint8_t) clamp<double>(color.red,   0, 255),
        (uint8_t) clamp<double>(color.green, 0, 255),
        (uint8_t) clamp<double>(color.blue,  0, 255),
        (uint8_t) clamp<double>(color.alpha, 0, 255)
    };
    
    if (p->cpu)
    {
        if (x < 0 || y < 0 || x >= p->cpu->w || y >= p->cpu->h)
            return;
        getPixelAt(p->cpu, p->format, x, y) = SDL_MapRGBA(p->format, pixel[0], pixel[1], pixel[2], pixel[3]);
        p->addTaintedArea(IntRect(x, y, 1, 1));
        cpuTouch(p, IntRect(x, y, 1, 1));
        p->onModified(false);
        return;
    }

    TEX::bind(p->gl.tex);
    TEX::uploadSubImage(x, y, 1, 1, &pixel, GL_RGBA);
    relinkAfterUpload(p->gl);
    
    p->addTaintedArea(IntRect(x, y, 1, 1));
    
    /* Setting just a single pixel is no reason to throw away the
     * whole cached surface; we can just apply the same change */
    
    if (p->surface)
    {
        uint32_t &surfPixel = getPixelAt(p->surface, p->format, x, y);
        surfPixel = SDL_MapRGBA(p->format, pixel[0], pixel[1], pixel[2], pixel[3]);
    }
    
    p->onModified(false);
}

bool Bitmap::getRaw(void *output, int output_size)
{
    if (output_size != width()*height()*4) return false;
    
    guardDisposed();
    if (p->cpu)
    {
        for (int y = 0; y < p->cpu->h; ++y)
            memcpy((uint8_t *)output + (size_t)y * p->cpu->w * 4,
                   (const uint8_t *)p->cpu->pixels + (size_t)y * p->cpu->pitch, (size_t)p->cpu->w * 4);
        return true;
    }
    
    if (hasHires()) {
        Debug() << "GAME BUG: Game is calling getRaw on low-res Bitmap; you may want to patch the game to improve graphics quality.";
    }

    /* Raw pixel observation; materialize first. */
    clearRead(p);

    if (!p->animation.enabled && (p->surface || p->megaSurface)) {
        void *src = (p->megaSurface) ? p->megaSurface->pixels : p->surface->pixels;
        memcpy(output, src, output_size);
    }
    else {
#ifdef __vita__
        /* Same fast path as getPixel: ordinary RGBA8 texture storage is
         * CPU-visible mapped memory. Read it directly instead of
         * glReadPixels, which faulted the physical GPU on FBO readback
         * during game-save snapshots. Row pitch matches getPixel. */
        glFinish();
        const Uint8 *texturePixels = static_cast<const Uint8 *>(
            vglGetTexDataPointerByID(getGLTypes().tex.gl));
        if (texturePixels)
        {
            const size_t sourcePitch =
                static_cast<size_t>((width() + 7) & ~7) * 4;
            const size_t copyWidth = static_cast<size_t>(width()) * 4;
            Uint8 *outPixels = static_cast<Uint8 *>(output);
            for (int row = 0; row < height(); ++row)
                std::memcpy(outPixels + row * copyWidth,
                            texturePixels + row * sourcePitch, copyWidth);
        }
        else
        {
            vitaDiagLog("ERROR", "GETRAW texture_data_missing tex=%u size=%dx%d",
                        getGLTypes().tex.gl, width(), height());
            FBO::bind(getGLTypes().fbo);
            gl.ReadPixels(0,0,width(),height(),GL_RGBA,GL_UNSIGNED_BYTE,output);
        }
#else
        FBO::bind(getGLTypes().fbo);
        gl.ReadPixels(0,0,width(),height(),GL_RGBA,GL_UNSIGNED_BYTE,output);
#endif
    }
    return true;
}

void Bitmap::replaceRaw(void *pixel_data, int size)
{
    guardDisposed();
    /* No CPU version of this operation. */
    cpuPromote(p);
    
    GUARD_MEGA;
    
    if (hasHires()) {
        Debug() << "GAME BUG: Game is calling replaceRaw on low-res Bitmap; you may want to patch the game to improve graphics quality.";
    }

    int w = width();
    int h = height();
    int requiredsize = w*h*4;
    
    if (size != w*h*4)
        throw Exception(Exception::MKXPError, "Replacement bitmap data is not large enough (given %i bytes, need %i)", size, requiredsize);

    /* Full CPU overwrite: the pending clear is dead (no pixels survive),
     * so drop it without GPU work. Counts as a draw-fusion. */
    if (p->pendingClear)
    {
        p->pendingClear = false;
        FrameProfile::Scope fused(FrameProfile::ClearNextDraw);
    }

    TEXFBO::uploadImage(getGLTypes(), w, h, pixel_data, GL_RGBA);
    relinkAfterUpload(getGLTypes());
    
    taintArea(IntRect(0,0,w,h));
    p->onModified();
}

void Bitmap::saveToFile(const char *filename)
{
    guardDisposed();
    /* No CPU version of this operation. */
    cpuPromote(p);
    
    if (hasHires()) {
        Debug() << "GAME BUG: Game is calling saveToFile on low-res Bitmap; you may want to patch the game to improve graphics quality.";
    }

    /* File snapshot observes pixels; materialize first. */
    clearRead(p);

    SDL_Surface *surf;
    
    if (p->surface || p->megaSurface) {
        surf = (p->surface) ? p->surface : p->megaSurface;
    }
    else {
        surf = SDL_CreateRGBSurface(0, width(), height(),p->format->BitsPerPixel, p->format->Rmask,p->format->Gmask,p->format->Bmask,p->format->Amask);
        
        if (!surf)
            throw Exception(Exception::SDLError, "Failed to prepare bitmap for saving: %s", SDL_GetError());
        
        getRaw(surf->pixels, surf->w * surf->h * 4);
    }
    
    // Try and determine the intended image format from the filename extension
    const char *period = strrchr(filename, '.');
    int filetype = 0;
    if (period) {
        period++;
        std::string ext;
        for (int i = 0; i < (int)strlen(period); i++) {
            ext += tolower(period[i]);
        }
        
        if (!ext.compare("png")) {
            filetype = 1;
        }
        else if (!ext.compare("jpg") || !ext.compare("jpeg")) {
            filetype = 2;
        }
    }
    
    std::string fn_normalized = shState->fileSystem().normalize(filename, 1, 1);
    int rc;
    switch (filetype) {
        case 2:
            rc = IMG_SaveJPG(surf, fn_normalized.c_str(), 90);
            break;
        case 1:
            rc = IMG_SavePNG(surf, fn_normalized.c_str());
            break;
        case 0: default:
            rc = SDL_SaveBMP(surf, fn_normalized.c_str());
            break;
    }
    
    if (!p->surface && !p->megaSurface)
        SDL_FreeSurface(surf);
    
    if (rc) throw Exception(Exception::SDLError, "%s", SDL_GetError());
}

void Bitmap::hueChange(int hue)
{
    guardDisposed();
    /* No CPU version of this operation. */
    cpuPromote(p);
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    
    if (hasHires()) {
        p->selfHires->hueChange(hue);
        return;
    }

    /* Hue pass samples existing pixels; materialize first. */
    clearRead(p);

    if ((hue % 360) == 0)
        return;
    
    TEXFBO newTex = shState->texPool().request(width(), height());
    
    FloatRect texRect(rect());
    
    Quad &quad = shState->gpQuad();
    quad.setTexPosRect(texRect, texRect);
    quad.setColor(Vec4(1, 1, 1, 1));
    
    HueShader &shader = shState->shaders().hue;
    shader.bind();
    /* Shader expects normalized value */
    shader.setHueAdjust(wrapRange(hue, 0, 359) / 360.0f);
    
    FBO::bind(newTex.fbo);
    p->pushSetViewport(shader);
    p->bindTexture(shader, false);
    
    p->blitQuad(quad);
    
    p->popViewport();
    
    TEX::unbind();
    
    shState->texPool().release(p->gl);
    p->gl = newTex;
    
    p->onModified();
}

void Bitmap::drawText(int x, int y,
                      int width, int height,
                      const char *str, int align)
{
    drawText(IntRect(x, y, width, height), str, align);
}

/* ---- rendered text cache ---------------------------------------------------
 *
 * Rasterizing a string (FreeType, then shadow and outline composition) takes
 * about a millisecond on the Vita, and games redraw the same strings over
 * and over: a status window refresh repeats every name and number it showed
 * a second ago. Finished text surfaces are kept, most recently used last,
 * under a small byte budget. The key holds everything the pixels depend on. */
struct TextCacheEntry
{
    SDL_Surface *surface;
    std::list<const std::string *>::iterator order;
};
static std::unordered_map<std::string, TextCacheEntry> textCache;
static std::list<const std::string *> textCacheOrder;
static size_t textCacheBytes = 0;
static const size_t textCacheBudget = 4u * 1024 * 1024;

/* Text surfaces are 32-bit; a copy is one small allocation and a memcpy. */
static SDL_Surface *textCacheCopy(SDL_Surface *from)
{
    SDL_Surface *copy = SDL_CreateRGBSurface(0, from->w, from->h, from->format->BitsPerPixel,
                                             from->format->Rmask, from->format->Gmask,
                                             from->format->Bmask, from->format->Amask);
    if (copy)
        for (int y = 0; y < from->h; ++y)
            memcpy((uint8_t *)copy->pixels + (size_t)y * copy->pitch,
                   (const uint8_t *)from->pixels + (size_t)y * from->pitch, (size_t)from->w * 4);
    return copy;
}

/* A copy the caller owns, or null when the text has to be rendered. */
static SDL_Surface *textCacheFind(const std::string &key)
{
    std::unordered_map<std::string, TextCacheEntry>::iterator it = textCache.find(key);
    if (it == textCache.end())
        return 0;
    textCacheOrder.splice(textCacheOrder.end(), textCacheOrder, it->second.order);
    return textCacheCopy(it->second.surface);
}

static void textCacheStore(const std::string &key, SDL_Surface *surface)
{
    const size_t bytes = (size_t)surface->pitch * surface->h;
    if (surface->format->BitsPerPixel != 32 || bytes > textCacheBudget / 8 || textCache.count(key))
        return;
    while (textCacheBytes + bytes > textCacheBudget && !textCacheOrder.empty())
    {
        std::unordered_map<std::string, TextCacheEntry>::iterator old = textCache.find(*textCacheOrder.front());
        textCacheOrder.pop_front();
        textCacheBytes -= (size_t)old->second.surface->pitch * old->second.surface->h;
        SDL_FreeSurface(old->second.surface);
        textCache.erase(old);
    }
    /* The cache keeps its own copy; the caller still owns the original. */
    surface = textCacheCopy(surface);
    if (!surface)
        return;
    std::pair<std::unordered_map<std::string, TextCacheEntry>::iterator, bool> added =
        textCache.insert(std::make_pair(key, TextCacheEntry()));
    added.first->second.surface = surface;
    added.first->second.order = textCacheOrder.insert(textCacheOrder.end(), &added.first->first);
    textCacheBytes += bytes;
}

static void textCacheClear()
{
    for (std::unordered_map<std::string, TextCacheEntry>::iterator it = textCache.begin(); it != textCache.end(); ++it)
        SDL_FreeSurface(it->second.surface);
    textCache.clear();
    textCacheOrder.clear();
    textCacheBytes = 0;
}

static std::string fixupString(const char *str)
{
    std::string s(str);
    
    /* RMXP actually draws LF as a "missing gylph" box,
     * but since we might have accidentally converted CRs
     * to LFs when editing scripts on a Unix OS, treat them
     * as white space too */
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '\r' || s[i] == '\n')
            s[i] = ' ';
    
    return s;
}

static void applyShadow(SDL_Surface *&in, const SDL_PixelFormat &fm, const SDL_Color &c, int offset)
{
    FrameProfile::Scope profile(FrameProfile::Shadow);
    SDL_Surface *out = SDL_CreateRGBSurface
    (0, in->w+offset, in->h+offset, fm.BitsPerPixel, fm.Rmask, fm.Gmask, fm.Bmask, fm.Amask);
    
    float fr = c.r / 255.0f;
    float fg = c.g / 255.0f;
    float fb = c.b / 255.0f;
    
    /* We allocate an output surface one pixel wider and higher than the input,
     * (implicitly) blit a copy of the input with RGB values set to black into
     * it with x/y offset by 1, then blend the input surface over it at origin
     * (0,0) using the bitmap blit equation (see shader/bitmapBlit.frag) */
    
    for (int y = 0; y < in->h+offset; ++y)
        for (int x = 0; x < in->w+offset; ++x)
        {
            /* src: input pixel, shd: shadow pixel */
            uint32_t src = 0, shd = 0;
            
            /* Output pixel location */
            uint32_t *outP = ((uint32_t*) ((uint8_t*) out->pixels + y*out->pitch)) + x;
            
            if (y < in->h && x < in->w)
                src = ((uint32_t*) ((uint8_t*) in->pixels + y*in->pitch))[x];
            
            if (y >= offset && x >= offset)
                shd = ((uint32_t*) ((uint8_t*) in->pixels + (y-offset)*in->pitch))[x-offset];
            
            /* Set shadow pixel RGB values to 0 (black) */
            shd &= fm.Amask;
            
            if (x < offset || y < offset)
            {
                *outP = src;
                continue;
            }
            
            if (x >= in->w || y >= in->h)
            {
                *outP = shd;
                continue;
            }
            
            /* Input and shadow alpha values */
            uint8_t srcA, shdA;
            srcA = (src & fm.Amask) >> fm.Ashift;
            shdA = (shd & fm.Amask) >> fm.Ashift;
            
            if (srcA == 255 || shdA == 0)
            {
                *outP = src;
                continue;
            }
            
            if (srcA == 0 && shdA == 0)
            {
                *outP = 0;
                continue;
            }
            
            float fSrcA = srcA / 255.0f;
            float fShdA = shdA / 255.0f;
            
            /* Because opacity == 1, co1 == fSrcA */
            float co2 = fShdA * (1.0f - fSrcA);
            /* Result alpha */
            float fa = fSrcA + co2;
            /* Temp value to simplify arithmetic below */
            float co3 = fSrcA / fa;
            
            /* Result colors */
            uint8_t r, g, b, a;
            
            r = clamp<float>(fr * co3, 0, 1) * 255.0f;
            g = clamp<float>(fg * co3, 0, 1) * 255.0f;
            b = clamp<float>(fb * co3, 0, 1) * 255.0f;
            a = clamp<float>(fa, 0, 1) * 255.0f;
            
            *outP = SDL_MapRGBA(&fm, r, g, b, a);
        }
    
    /* Store new surface in the input pointer */
    SDL_FreeSurface(in);
    in = out;
}

/* An implementation of the bitmap blit equation (see shader/bitmapBlit.frag),
 * modified for combining text with its outline. */
static inline void blendText(SDL_Surface *txtSrf, const SDL_Rect &inRect, const SDL_Color &inColor,
                           SDL_Surface *outSrf, const SDL_Rect &outRect, const SDL_Color &outColor, bool hasShadow)
{
    FrameProfile::Scope profile(FrameProfile::OutlineBlend);
    size_t offset = (inRect.x * txtSrf->format->BytesPerPixel) + (inRect.y * txtSrf->pitch);
    uint8_t *txtStart = (uint8_t*)txtSrf->pixels + offset;
    offset = (outRect.x * outSrf->format->BytesPerPixel) + (outRect.y * outSrf->pitch);
    uint8_t *outStart = (uint8_t*)outSrf->pixels + offset;
    
    // SDL_TTF sets every pixel to the same RGB value and just adjusts the alpha
    float txtR = inColor.r;
    float txtG = inColor.g;
    float txtB = inColor.b;
    float outR = outColor.r;
    float outG = outColor.g;
    float outB = outColor.b;
    
    /* SDL_ttf blends the glyphs together, which causes overlapping
     * transparent pixels to get too opaque. RGSS probably does it, too,
     * and you'd probably have to zoom in to see it, but if you do see it
     * then it looks kind of ugly so we'll fix it.
     * I don't know if it can actually happen for non-outline text,
     * but we'll handle it, too, just in case.
     * We don't do it for non-outline text if there's a shadow, because I'm not sure how to do this workaround with shadows. */
    uint32_t fullTxtPixel = SDL_MapRGBA(outSrf->format, inColor.r, inColor.g, inColor.b, inColor.a);
    uint32_t fullOutPixel = SDL_MapRGBA(outSrf->format, outColor.r, outColor.g, outColor.b, outColor.a);
    
    for (int i=0; i < inRect.h; ++i)
    {
        uint32_t *txtPixel = (uint32_t*)(txtStart + i*txtSrf->pitch);
        uint32_t *outPixel = (uint32_t*)(outStart + i*outSrf->pitch);
        for (int j=0; j < inRect.w; ++j)
        {
            uint8_t txtA = (*txtPixel >> txtSrf->format->Ashift) & 0xFF;
            uint8_t outA = (*outPixel >> outSrf->format->Ashift) & 0xFF;
            
            if (txtA >= inColor.a)
            {
                if (hasShadow)
                {
                    *outPixel = *txtPixel;
                }
                else
                {
                    *outPixel = fullTxtPixel;
                }
            } else if (outA == 0) {
                *outPixel = *txtPixel;
            } else if (txtA != 0) {
                /* Use the full text opacity instead of 255. */
                int32_t co1 = (int)txtA * inColor.a;
                int32_t co2 = (int)std::min(outA, outColor.a) * (inColor.a - txtA);
                
                /* Result alpha */
                int32_t fa = co1 + co2;
                
                /* Result colors */
                uint8_t r, g, b, a;
                
                float faInv = 1.0f / fa;
                float co3 = co1 * faInv;
                float co4 = co2 * faInv;

                if (hasShadow)
                {
                    txtR = (*txtPixel >> txtSrf->format->Rshift) & 0xFF;
                    txtG = (*txtPixel >> txtSrf->format->Gshift) & 0xFF;
                    txtB = (*txtPixel >> txtSrf->format->Bshift) & 0xFF;
                }

                // Adding a small number to combat floating point errors.
                r = std::min<int>((txtR * co3 + outR * co4) + 0.001f, 255);
                g = std::min<int>((txtG * co3 + outG * co4) + 0.001f, 255);
                b = std::min<int>((txtB * co3 + outB * co4) + 0.001f, 255);
                
                /* RGSS seems to not round, but our blit shader seemingly does. */
                a = fa / inColor.a;
                
                *outPixel = SDL_MapRGBA(outSrf->format, r, g, b, a);
            } else if (outA > outColor.a) {
                /* SDL_ttf blends the glyphs together, which causes overlapping
                 * transparent pixels to get too opaque. */
                *outPixel = fullOutPixel;
            }
            
            ++txtPixel;
            ++outPixel;
        }
    }
}

namespace {
// Draw calls run on the RGSS thread. POD storage avoids startup-time guards.
struct TextTimingTotals {
    Uint64 count;
    double ms[8];
};
static TextTimingTotals textTimingTotals = {};
class TextTiming {
    bool enabled;
    Uint64 previous;
    double frequency;
    double elapsed[8];
public:
    TextTiming() : enabled(vitaDiagTextProfileEnabled()), previous(0),
                   frequency(1), elapsed{} {
        if (enabled) {
            frequency = static_cast<double>(SDL_GetPerformanceFrequency());
            previous = SDL_GetPerformanceCounter();
        }
    }
    void stage(int index) {
        if (!enabled) return;
        const Uint64 now = SDL_GetPerformanceCounter();
        elapsed[index] += (now - previous) * 1000.0 / frequency;
        previous = now;
    }
    void finish() {
        if (!enabled) return;
        const Uint64 count = ++textTimingTotals.count;
        for (int i = 0; i < 8; ++i) textTimingTotals.ms[i] += elapsed[i];
        // Sparse output includes later menus after startup has warmed the font.
        if ((count & (count - 1)) != 0 && count % 16 != 0) return;
        vitaDiagLog("TEXTTIME", "draws=%llu mean_ms prep=%.3f measure=%.3f raster=%.3f format_shadow=%.3f outline=%.3f upload=%.3f blit=%.3f cleanup=%.3f",
            static_cast<unsigned long long>(count),
            textTimingTotals.ms[0]/count, textTimingTotals.ms[1]/count,
            textTimingTotals.ms[2]/count, textTimingTotals.ms[3]/count,
            textTimingTotals.ms[4]/count, textTimingTotals.ms[5]/count,
            textTimingTotals.ms[6]/count, textTimingTotals.ms[7]/count);
    }
};
}

void Bitmap::drawText(const IntRect &rect, const char *str, int align)
{
    FrameProfile::Scope profile(FrameProfile::TextPrep);
    TextTiming timing;
    guardDisposed();

    vitaDiagLog("TEXTFLOW", "draw_text_begin rect=%d,%d %dx%d align=%d bytes=%lu outline=%d",
                rect.x, rect.y, rect.w, rect.h, align,
                static_cast<unsigned long>(str ? std::strlen(str) : 0),
                p->font ? (p->font->getOutline() ? 1 : 0) : -1);
    vitaDiagLogVglMemory("draw_text_begin");
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    
    // RGSS doesn't let you draw text backwards
    if (rect.w <= 0 || rect.h <= 0 || rect.x >= width() || rect.y >= height() ||
        rect.w < -rect.x || rect.h < -rect.y)
        return;
    
    if (hasHires()) {
        p->selfHires->guardDisposed();
        p->selfHires->setFont(getFont());

        int rectX = rect.x * p->selfHires->width() / width();
        int rectY = rect.y * p->selfHires->height() / height();
        int rectWidth = rect.w * p->selfHires->width() / width();
        int rectHeight = rect.h * p->selfHires->height() / height();

        p->selfHires->drawText(IntRect(rectX, rectY, rectWidth, rectHeight), str, align);

        return;
    }

    std::string fixed = fixupString(str);
    str = fixed.c_str();
    
    if (*str == '\0')
        return;
    
    if (str[0] == ' ' && str[1] == '\0')
        return;
    
    TTF_Font *sdlFont = p->font->getSdlFont(0);
    const Color &fontColor = p->font->getColor();
    const Color &outColor = p->font->getOutColor();
    
    SDL_Color c = fontColor.toSDLColor();
    
    if (c.a == 0)
        return;
    
    // RGSS crops the the text slightly if there's an outline
    int scaledOutlineSize = 0;
    SDL_Color co;
    if (p->font->getOutline()) {
        // Handle high-res for outline.
        if (p->selfLores) {
            scaledOutlineSize = OUTLINE_SIZE * width() / p->selfLores->width();
        } else {
            scaledOutlineSize = OUTLINE_SIZE;
        }
        
        /* RGSS's outline is drawn by blitting a complete set of text four times, offset diagonally.
         * However, this looks very ugly in hires mode, so instead we'll fake the effect by
         * precomputing the final outline and text colors. */
        co = outColor.toSDLColor();

        if (c.a != 255) {
            vitaDiagLog("TEXT", "draw_text uses approximate translucent outline blending");
        }

        if (c.a != 255 || co.a != 255) {
            /* Step 1: Compute the outline alpha by layering it onto itself */
            uint8_t out_alpha = ((int)co.a * (int)c.a) / 255;
            
            int co1 = out_alpha * 255;
            int co2 = out_alpha * (255 - out_alpha);
            int fa = co1 + co2;
            co.a = (fa + 1 + (fa >> 8)) >> 8;
            /* Use this instead if we decide we want to round 
             * RGSS seems to not round, but our blit shader seemingly does. */
            //co.a = (fa + 128 + ((fa + 128) >> 8)) >> 8;
            
            if (c.a != 255) {
                /* Step 2: Compute the opacity of the outline that would have been drawn behind the text.
                 * In RGSS, there's a 1 pixel wide region at the edge of the text that only has
                 * 2 layers of outline instead of the 4 layers that's behind most of the text,
                 * which combined with the outlines having less opaque corners from how they're drawn
                 * slightly affects the appearance of the text. We can't replicate this in a way that
                 * looks nice in hires mode, however, this will have to be good enough. */
                uint8_t out_alpha_full = co.a; // compute outline alpha - 4 layers
                for (int i = 0; i < 2; ++i) {
                    int co1 = out_alpha * 255;
                    int co2 = out_alpha_full * (255 - out_alpha);
                    int fa = co1 + co2;
                    out_alpha_full = (fa + 1 + (fa >> 8)) >> 8;
                    /* Use this instead if we decide we want to round 
                     * RGSS seems to not round, but our blit shader seemingly does. */
                    //out_alpha_full = (fa + 128 + ((fa + 128) >> 8)) >> 8;
                }
                
                /* Step 3: Calculate the text color using out_alpha_full in place of co.a. */
                int co1 = c.a * 255;
                int co2 = out_alpha_full * (255 - c.a);
                int fa = co1 + co2;
                
                float faInv = 1.0f / fa;
                float co3 = co1 * faInv;
                float co4 = co2 * faInv;
                // Adding a small number to combat floating point errors.
                c.r = std::min<int>((c.r * co3 + co.r * co4) + 0.001f, 255);
                c.g = std::min<int>((c.g * co3 + co.g * co4) + 0.001f, 255);
                c.b = std::min<int>((c.b * co3 + co.g * co4) + 0.001f, 255);
                
                c.a = (fa + 1 + (fa >> 8)) >> 8;
                /* Use this instead if we decide we want to round 
                 * RGSS seems to not round, but our blit shader seemingly does. */
                //c.a = (fa + 128 + ((fa + 128) >> 8)) >> 8;
            }
        }
    }
    int doubleOutlineSize = scaledOutlineSize * 2;
    
    // Use the output of textSize to determine squeezing, since textSize tends to be used to determine
    // rect dimensions.
    // Also use it to determine position, because freetype sometimes treats the last character as
    // being a pixel wider than it should be, and which textSize is currently set to compensate for.
    timing.stage(0);
    profile.stage(FrameProfile::TextPrep + 1);
    int alignmentWidth, alignmentHeight;
    {
        const IntRect &text_size = textSize(str);
        alignmentWidth = text_size.w;
        alignmentHeight = text_size.h;
        
        if (!alignmentWidth)
            return;
    }
    
    // Trim the text to only fill double the rect width
    int charLimit = 0;
    float squeezeLimit = 0.5f;
    if (TTF_MeasureUTF8(sdlFont, str, std::min(width() - rect.x, rect.w) / squeezeLimit, nullptr, &charLimit) == 0)
    {
        if (charLimit != fixed.size())
        {
            /* TTF_MeasureUTF8 returns the charLimit in codepoints, not bytes,
             * so we have to calculate where that limit is ourselves.
             * Grabbing a few codepoints past the limit in case the next
             * character is a multi codepoint character.*/
            charLimit += 4;
            for(std::string::iterator it=fixed.begin(); it!=fixed.end() && *it != '\0'; ++it)
            {
                /* The first byte of a multibyte character starts with the first
                 * two bits set to 11, with subsequent bytes starting with 10.
                 * Single byte characters start with 0.*/
                if ((*it & 0xC0) != 0x80)
                {
                    if (charLimit-- == 0)
                    {
                        *it = '\0';
                        break;
                    }
                }
            }
        }
    }
    
    timing.stage(1);
    profile.stage(FrameProfile::TextPrep + 2);
    /* Everything the finished text surface depends on. The outline pass
     * crops by the rect, so its size is part of the key. */
    std::string textKey(str);
    {
        const SDL_Color keyColor = fontColor.toSDLColor(), keyOut = outColor.toSDLColor();
        struct
        {
            TTF_Font *font; int size, w, h, outline;
            uint8_t color[4], out[4], bold, italic, solid, shadow, crop;
        } fields;
        memset(&fields, 0, sizeof(fields)); /* padding is part of the key */
        fields.font = sdlFont; fields.size = p->font->getSize();
        fields.w = rect.w; fields.h = rect.h; fields.outline = scaledOutlineSize;
        fields.color[0] = keyColor.r; fields.color[1] = keyColor.g; fields.color[2] = keyColor.b; fields.color[3] = keyColor.a;
        fields.out[0] = keyOut.r; fields.out[1] = keyOut.g; fields.out[2] = keyOut.b; fields.out[3] = keyOut.a;
        fields.bold = p->font->getBold(); fields.italic = p->font->getItalic(); fields.solid = p->font->isSolid();
        fields.shadow = p->font->getShadow(); fields.crop = shState->config().fontOutlineCrop;
        textKey.push_back('\0');
        textKey.append((const char *)&fields, sizeof(fields));
    }
    SDL_Surface *txtSurf = textCacheFind(textKey);
    const bool textCached = txtSurf != 0;
    if (textCached)
        ;
    else if (p->font->isSolid())
        txtSurf = TTF_RenderUTF8_Solid(sdlFont, str, c);
    else
        txtSurf = TTF_RenderUTF8_Blended(sdlFont, str, c);
    
    timing.stage(2);
    profile.stage(FrameProfile::TextPrep + 3);
    if (!txtSurf)
        throw Exception(Exception::SDLError, "Error creating text: %s",
                        SDL_GetError());

    vitaDiagLog("TEXTFLOW", "draw_text_surface text=%p size=%dx%d format=%08x",
                (void *)txtSurf, txtSurf->w, txtSurf->h,
                txtSurf->format ? txtSurf->format->format : 0);

    if (!textCached)
        p->ensureFormat(txtSurf, SDL_PIXELFORMAT_ABGR8888);
    vitaDiagLog("TEXTFLOW", "draw_text_format_done text=%p size=%dx%d format=%08x",
                (void *)txtSurf, txtSurf->w, txtSurf->h,
                txtSurf->format ? txtSurf->format->format : 0);
    
    if (!textCached && p->font->getShadow())
    {
        int scaledShadowSize = 1;
        if (p->selfLores) {
            scaledShadowSize = scaledShadowSize * width() / p->selfLores->width();
        }

        applyShadow(txtSurf, *p->format, c, scaledShadowSize);
    }
    
    int alignX = rect.x;
    
    switch (align)
    {
        default:
        case Left :
            break;
            
        case Center :
            // Yes, half of the outline size.
            alignX += (rect.w - (alignmentWidth + scaledOutlineSize)) / 2;
            break;
            
        case Right :
            // I don't know why it's double the outline size, but it is.
            alignX += rect.w - alignmentWidth - doubleOutlineSize;
            break;
    }
    
    if (alignX < rect.x)
        alignX = rect.x;
    
    int alignY = rect.y + ((rect.h - alignmentHeight) / 2) - scaledOutlineSize;
    
    alignY = std::max(alignY, rect.y);
    
    /* FIXME: RGSS begins squeezing the text before it fills the rect.
     * While this is extremely undesirable, a number of games will understandably
     * have made the rects bigger to compensate, so we should probably match it */
    float squeeze = (float) rect.w / alignmentWidth;
    
    squeeze = clamp(squeeze, squeezeLimit, 1.0f);

    timing.stage(3);
    profile.stage(FrameProfile::TextPrep + 4);
    if (scaledOutlineSize && !textCached)
    {
        SDL_Surface *outline;
        TTF_Font *sdlOutline;
        try {
            sdlOutline = p->font->getSdlFont(scaledOutlineSize);
        } catch (const Exception &e) {
            SDL_FreeSurface(txtSurf);
            throw e;
        }
        if (p->font->isSolid())
            outline = TTF_RenderUTF8_Solid(sdlOutline, str, co);
        else
            outline = TTF_RenderUTF8_Blended(sdlOutline, str, co);
        
        if (!outline) {
            SDL_FreeSurface(txtSurf);
            throw Exception(Exception::SDLError, "Error creating text outline: %s",
                            SDL_GetError());
        }

        vitaDiagLog("TEXTFLOW", "draw_text_outline_surface outline=%p size=%dx%d format=%08x",
                    (void *)outline, outline->w, outline->h,
                    outline->format ? outline->format->format : 0);

        p->ensureFormat(outline, SDL_PIXELFORMAT_ABGR8888);
        vitaDiagLog("TEXTFLOW", "draw_text_outline_format_done outline=%p size=%dx%d format=%08x",
                    (void *)outline, outline->w, outline->h,
                    outline->format ? outline->format->format : 0);

        // Enterbrain's runtime crops the top row and left column of the text
        // when blitting it onto the outline. We allow the user to optionally
        // disable this cropping, since it's arguably quite ugly.
        int outlineCropUndo = shState->config().fontOutlineCrop ? 0 : scaledOutlineSize;

        /* outline should always be at least doubleOutlineSize bigger than txtSurf,
         * but we may as well validate it here anyway. */
        SDL_Rect inRect = {scaledOutlineSize - outlineCropUndo, scaledOutlineSize - outlineCropUndo,
                           std::min<int>({(int)(rect.w / squeeze) - doubleOutlineSize,
                                          txtSurf->w - scaledOutlineSize,
                                          outline->w - doubleOutlineSize
                                         }) + outlineCropUndo,
                           std::min<int>({rect.h - doubleOutlineSize,
                                          txtSurf->h - scaledOutlineSize,
                                          outline->h - doubleOutlineSize
                                         }) + outlineCropUndo};
        SDL_Rect outRect = {doubleOutlineSize - outlineCropUndo, doubleOutlineSize - outlineCropUndo, 0, 0};
        
        vitaDiagLog("TEXTFLOW", "draw_text_blend_begin text=%p outline=%p in=%d,%d %dx%d out=%d,%d",
                    (void *)txtSurf, (void *)outline,
                    inRect.x, inRect.y, inRect.w, inRect.h,
                    outRect.x, outRect.y);
        blendText(txtSurf, inRect, c, outline, outRect, co, p->font->getShadow());
        vitaDiagLog("TEXTFLOW", "draw_text_blend_done outline=%p size=%dx%d",
                    (void *)outline, outline->w, outline->h);
        SDL_FreeSurface(txtSurf);
        txtSurf = outline;
    }
    
    if (!textCached)
        textCacheStore(textKey, txtSurf);

    timing.stage(4);
    profile.stage(FrameProfile::TextPrep + 5);
    IntRect destRect(alignX, alignY,
                    std::min(rect.w, (int)(txtSurf->w * squeeze)),
                    std::min(rect.h, txtSurf->h));
    
    destRect.w = std::min(destRect.w, width() - destRect.x);
    destRect.h = std::min(destRect.h, height() - destRect.y);
    
    IntRect sourceRect(scaledOutlineSize, scaledOutlineSize, destRect.w / squeeze, destRect.h);
    
    {
    Bitmap txtBitmap(txtSurf, nullptr, true);
    timing.stage(5);
    profile.stage(FrameProfile::TextPrep + 6);
    bool smooth = squeeze != 1.0f;
    vitaDiagLog("TEXTFLOW", "draw_text_stretch_begin text=%p dest=%d,%d %dx%d source=%d,%d %dx%d smooth=%d",
                (void *)txtSurf, destRect.x, destRect.y, destRect.w, destRect.h,
                sourceRect.x, sourceRect.y, sourceRect.w, sourceRect.h,
                smooth ? 1 : 0);
    stretchBlt(destRect, txtBitmap, sourceRect, 255, smooth);
    timing.stage(6);
    profile.stage(FrameProfile::TextPrep + 7);
    }
    timing.stage(7);
    timing.finish();
    vitaDiagLog("TEXTFLOW", "draw_text_done");
}

/* http://www.lemoda.net/c/utf8-to-ucs2/index.html */
static uint16_t utf8_to_ucs2(const char *_input,
                             const char **end_ptr)
{
    const unsigned char *input =
    reinterpret_cast<const unsigned char*>(_input);
    *end_ptr = _input;
    
    if (input[0] == 0)
        return -1;
    
    if (input[0] < 0x80)
    {
        *end_ptr = _input + 1;
        
        return input[0];
    }
    
    if ((input[0] & 0xE0) == 0xE0)
    {
        if (input[1] == 0 || input[2] == 0)
            return -1;
        
        *end_ptr = _input + 3;
        
        return (input[0] & 0x0F)<<12 |
        (input[1] & 0x3F)<<6  |
        (input[2] & 0x3F);
    }
    
    if ((input[0] & 0xC0) == 0xC0)
    {
        if (input[1] == 0)
            return -1;
        
        *end_ptr = _input + 2;
        
        return (input[0] & 0x1F)<<6  |
        (input[1] & 0x3F);
    }
    
    return -1;
}

IntRect Bitmap::textSize(const char *str)
{
    guardDisposed();
    
    GUARD_MEGA;
    GUARD_ANIMATED;
    
    // TODO: High-res Bitmap textSize not implemented, but I think it's the same as low-res?
    // Need to double-check this.

    TTF_Font *sdlFont = p->font->getSdlFont(0);
    
    // freetype sometimes treats the last character of the string as being
    // a pixel wider than it should be. Adding a space at the end and then
    // removing it's width should make character-by-character text
    // more accurate.
    std::string fixed = fixupString(str) + " ";
    
    int w, h;
    TTF_SizeUTF8(sdlFont, fixed.c_str(), &w, &h);
    
    int ws;
    TTF_SizeUTF8(sdlFont, " ", &ws, 0);
    w -= ws;
    
    /* If str is one character long, *endPtr == 0 */
    const char *endPtr;
    uint16_t ucs2 = utf8_to_ucs2(str, &endPtr);
    
    /* For cursive characters, returning the advance
     * as width yields better results */
    if (p->font->getItalic() && *endPtr == '\0')
        TTF_GlyphMetrics(sdlFont, ucs2, 0, 0, 0, 0, &w);

    if (shState->config().fontHeightReporting == 0) {
        if(!w) {
            h = 0;
        } else {
            /* RGSS normalizes the reported heights.
             * Note that this may result in the bottoms
             * of some characters being cut off. */
             h = TTF_FontHeight(sdlFont);
        }
    }
    
    return IntRect(0, 0, w, h);
}

DEF_ATTR_RD_SIMPLE(Bitmap, Font, Font&, *p->font)

void Bitmap::setFont(Font &value)
{
    *p->font = value;
}

void Bitmap::setInitFont(Font *value)
{
    if (value != &shState->defaultFont()) {
        if (p->selfLores)
            value->setHiresMult((float)width() / (float)p->selfLores->width());
        else
            value->setHiresMult(1.0f);
    }

    p->font = value;
}

TEXFBO &Bitmap::getGLTypes() const
{
    /* Direct texture access (tilemap atlas builds, sprite pattern sampling,
     * transition maps) observes pixels; materialize a pending clear so the
     * sampled storage matches the logical cleared state. */
    clearRead(p);
    return p->getGLTypes();
}

SDL_Surface *Bitmap::surface() const
{
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap surface not implemented";
    }

    return p->cpu ? p->cpu : p->surface;
}

SDL_Surface *Bitmap::megaSurface() const
{
    if (hasHires()) {
        if (p->megaSurface) {
            Debug() << "BUG: High-res Bitmap megaSurface not implemented (low-res has megaSurface)";
        }
        if (p->selfHires->megaSurface()) {
            Debug() << "BUG: High-res Bitmap megaSurface not implemented (high-res has megaSurface)";
        }
    }

    return p->megaSurface;
}

void Bitmap::ensureNonMega() const
{
    if (isDisposed())
        return;
    
    GUARD_MEGA;
}

void Bitmap::ensureNonAnimated() const
{
    if (isDisposed())
        return;
    
    GUARD_ANIMATED;
}

void Bitmap::ensureAnimated() const
{
    if (isDisposed())
        return;
    
    GUARD_UNANIMATED;
}

void Bitmap::stop()
{
    guardDisposed();
    
    GUARD_UNANIMATED;
    if (!p->animation.playing) return;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap stop not implemented";
    }

    p->animation.stop();
}

void Bitmap::play()
{
    guardDisposed();
    
    GUARD_UNANIMATED;
    if (p->animation.playing) return;

    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap play not implemented";
    }

    p->animation.play();
}

bool Bitmap::isPlaying() const
{
    guardDisposed();
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap isPlaying not implemented";
    }

    if (!p->animation.playing)
        return false;
    
    if (p->animation.loop)
        return true;
    
    return p->animation.currentFrameIRaw() < p->animation.frames.size();
}

void Bitmap::gotoAndStop(int frame)
{
    guardDisposed();
    
    GUARD_UNANIMATED;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap gotoAndStop not implemented";
    }

    p->animation.stop();
    p->animation.seek(frame);
}
void Bitmap::gotoAndPlay(int frame)
{
    guardDisposed();
    
    GUARD_UNANIMATED;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap gotoAndPlay not implemented";
    }

    p->animation.stop();
    p->animation.seek(frame);
    p->animation.play();
}

int Bitmap::numFrames() const
{
    guardDisposed();
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap numFrames not implemented";
    }

    if (!p->animation.enabled) return 1;
    return (int)p->animation.frames.size();
}

int Bitmap::currentFrameI() const
{
    guardDisposed();
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap currentFrameI not implemented";
    }

    if (!p->animation.enabled) return 0;
    return p->animation.currentFrameI();
}

int Bitmap::addFrame(Bitmap &source, int position)
{
    guardDisposed();
    /* No CPU version of this operation. */
    cpuPromote(p);
    source.guardDisposed();
    
    GUARD_MEGA;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap addFrame dest not implemented";
    }

    if (source.hasHires()) {
        Debug() << "BUG: High-res Bitmap addFrame source not implemented";
    }

    if (source.height() != height() || source.width() != width())
        throw Exception(Exception::MKXPError, "Animations with varying dimensions are not supported (%ix%i vs %ix%i)",
                        source.width(), source.height(), width(), height());
    
    TEXFBO newframe = shState->texPool().request(source.width(), source.height());
    
    // Convert the bitmap into an animated bitmap if it isn't already one
    if (!p->animation.enabled) {
        p->animation.width = p->gl.width;
        p->animation.height = p->gl.height;
        p->animation.enabled = true;
        p->animation.lastFrame = 0;
        p->animation.playTime = 0;
        p->animation.startTime = 0;
        
        if (p->animation.fps <= 0)
            p->animation.fps = shState->graphics().getFrameRate();
        
        p->animation.frames.push_back(p->gl);
        
        p->dropReadback();
        p->gl = TEXFBO();
    }
    
    if (source.surface()) {
        TEXFBO::uploadImage(newframe, source.width(), source.height(), source.surface()->pixels, GL_RGBA);
        p->dropReadback();
    }
    else {
        /* Source frame pixels observed by the copy pass; materialize. */
        clearRead(source.p);
        GLMeta::blitBegin(newframe, false, SameScale);
        GLMeta::blitSource(source.getGLTypes(), SameScale);
        GLMeta::blitRectangle(rect(), rect());
        GLMeta::blitEnd();
    }
    
    int ret;
    
    if (position < 0) {
        p->animation.frames.push_back(newframe);
        ret = (int)p->animation.frames.size();
    }
    else {
        p->animation.frames.insert(p->animation.frames.begin() + clamp(position, 0, (int)p->animation.frames.size()), newframe);
        ret = position;
    }
    
    return ret;
}

void Bitmap::removeFrame(int position) {
    guardDisposed();
    
    GUARD_UNANIMATED;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap removeFrame not implemented";
    }

    /* The last frame cannot go: the index below would be -1 or 0 of an empty vector. */
    if (p->animation.frames.size() < 2)
        throw Exception(Exception::MKXPError,
                        "Cannot remove the only frame of an animated bitmap");

    int pos = (position < 0) ? (int)p->animation.frames.size() - 1 : clamp(position, 0, (int)(p->animation.frames.size() - 1));
    shState->texPool().release(p->animation.frames[pos]);
    p->animation.frames.erase(p->animation.frames.begin() + pos);
    
    // Change the animated bitmap back to a normal one if there's only one frame left
    if (p->animation.frames.size() == 1) {
        
        p->animation.enabled = false;
        p->animation.playing = false;
        p->animation.width = 0;
        p->animation.height = 0;
        p->animation.lastFrame = 0;
        
        p->gl = p->animation.frames[0];
        p->animation.frames.erase(p->animation.frames.begin());
        
        FBO::bind(p->gl.fbo);
        taintArea(rect());
    }
}

void Bitmap::nextFrame()
{
    guardDisposed();
    
    GUARD_UNANIMATED;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap nextFrame not implemented";
    }

    stop();
    if ((uint32_t)p->animation.lastFrame >= p->animation.frames.size() - 1)  {
        if (!p->animation.loop) return;
        p->animation.lastFrame = 0;
        return;
    }
    
    p->animation.lastFrame++;
}

void Bitmap::previousFrame()
{
    guardDisposed();
    
    GUARD_UNANIMATED;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap previousFrame not implemented";
    }

    stop();
    if (p->animation.lastFrame <= 0) {
        if (!p->animation.loop) {
            p->animation.lastFrame = 0;
            return;
        }
        p->animation.lastFrame = (int)p->animation.frames.size() - 1;
        return;
    }
    
    p->animation.lastFrame--;
}

void Bitmap::setAnimationFPS(float FPS)
{
    guardDisposed();
    
    GUARD_MEGA;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap setAnimationFPS not implemented";
    }

    bool restart = p->animation.playing;
    p->animation.stop();
    p->animation.fps = (FPS < 0) ? 0 : FPS;
    if (restart) p->animation.play();
}

std::vector<TEXFBO> &Bitmap::getFrames() const
{
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap getFrames not implemented";
    }

    return p->animation.frames;
}

float Bitmap::getAnimationFPS() const
{
    guardDisposed();
    
    GUARD_MEGA;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap getAnimationFPS not implemented";
    }

    return p->animation.fps;
}

void Bitmap::setLooping(bool loop)
{
    guardDisposed();
    
    GUARD_MEGA;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap setLooping not implemented";
    }

    p->animation.loop = loop;
}

bool Bitmap::getLooping() const
{
    guardDisposed();
    
    GUARD_MEGA;
    
    if (hasHires()) {
        Debug() << "BUG: High-res Bitmap getLooping not implemented";
    }

    return p->animation.loop;
}

void Bitmap::kglInvert()
{
    guardDisposed();
    /* No CPU version of this operation. */
    cpuPromote(p);
    GUARD_ANIMATED;

    if (hasHires()) {
        p->selfHires->kglInvert();
        return;
    }

    /* Filter pass samples existing pixels via bindTexture; materialize. */
    clearRead(p);

    if (isMega()) {
        for (size_t i = 0; i < (size_t)p->megaSurface->w * (size_t)p->megaSurface->h; ++i) {
            for (size_t j = 0; j < 3; ++j) {
                ((uint8_t *)p->megaSurface->pixels)[4 * i + j] = ~((uint8_t *)p->megaSurface->pixels)[4 * i + j];
            }
        }
    } else {
        TEXFBO newTex = shState->texPool().request(width(), height());

        FloatRect texRect(rect());

        Quad &quad = shState->gpQuad();
        quad.setTexPosRect(texRect, texRect);
        quad.setColor(Vec4(1, 1, 1, 1));

        KglInvertShader &shader = shState->shaders().kglInvert;
        shader.bind();

        FBO::bind(newTex.fbo);
        p->pushSetViewport(shader);
        p->bindTexture(shader, false);

        p->blitQuad(quad);

        p->popViewport();

        TEX::unbind();

        shState->texPool().release(p->gl);
        p->gl = newTex;
    }

    p->onModified();
}

void Bitmap::kglCompressAlpha()
{
    guardDisposed();
    /* No CPU version of this operation. */
    cpuPromote(p);
    GUARD_ANIMATED;

    if (hasHires()) {
        p->selfHires->kglInvert();
        return;
    }

    /* Filter pass samples existing pixels via bindTexture; materialize. */
    clearRead(p);

    if (isMega()) {
        for (size_t i = 0; i < (size_t)p->megaSurface->w * (size_t)p->megaSurface->h; ++i) {
            for (size_t j = 0; j < 3; ++j) {
                ((uint8_t *)p->megaSurface->pixels)[4 * i + j] =
                    std::round(((float)((uint8_t *)p->megaSurface->pixels)[4 * i + 3] / 255.0f) * ((uint8_t *)p->megaSurface->pixels)[4 * i + j]);
            }
            ((uint8_t *)p->megaSurface->pixels)[4 * i + 3] = 0;
        }
    } else {
        TEXFBO newTex = shState->texPool().request(width(), height());

        FloatRect texRect(rect());

        Quad &quad = shState->gpQuad();
        quad.setTexPosRect(texRect, texRect);
        quad.setColor(Vec4(1, 1, 1, 1));

        KglCompressAlphaShader &shader = shState->shaders().kglCompressAlpha;
        shader.bind();

        FBO::bind(newTex.fbo);
        p->pushSetViewport(shader);
        p->bindTexture(shader, false);

        p->blitQuad(quad);

        p->popViewport();

        TEX::unbind();

        shState->texPool().release(p->gl);
        p->gl = newTex;
    }

    p->onModified();
}

int Bitmap::kglShadowShaderH(int x1, int x2, int y, bool soft)
{
    guardDisposed();
    /* No CPU version of this operation. */
    cpuPromote(p);
    GUARD_ANIMATED;

    if (hasHires()) {
        return p->selfHires->kglShadowShaderH(x1 * p->selfHires->width() / width(), x2 * p->selfHires->width() / width(), y * p->selfHires->height() / height(), soft);
    }

    int w = width();
    int h = height();

    if (y < 0 || y >= h || y == h / 2) {
        return 111;
    }

    if (w <= 0 || h <= 0) {
        return 1;
    }

    x1 = clamp(x1, 0, w - 1);
    x2 = clamp(x2, 0, w - 1);

    int x_center = w / 2;
    int y_center = h / 2;

    double slope1 = (double)(x1 - x_center) / (double)(y - y_center);
    double slope2 = (double)(x2 - x_center) / (double)(y - y_center);

    if (isMega()) {
        uint32_t *shadowbuffer = (uint32_t *)p->megaSurface->pixels;

        int y_start, y_end;
        if (y < y_center) {
            y_start = 0;
            y_end = y - 1;
        } else {
            y_start = y;
            y_end = h - 1;
        }

        for (int i = y_start; i <= y_end; ++i) {
            double x_start_raw = std::round(slope1 * (double)(i - y_center) + (double)x_center);
            double x_end_raw = std::round(slope2 * (double)(i - y_center) + (double)x_center + 0.2f); // The original shader contains a +0.2 adjustment factor for some reason

            int x_start = (int)clamp(x_start_raw, 0., (double)w + 3.);
            int x_end = (int)clamp(x_end_raw, -4., x2 < x_center ? (double)x2 - 1. : (double)w - 1.); // This bounds check is incorrect but is consistent with the original shader

            if (x_start <= x_end) {
                std::memset(
                    shadowbuffer + (size_t)w * (size_t)i + (size_t)x_start,
                    0,
                    (size_t)4 * ((size_t)x_end - (size_t)x_start + (size_t)1)
                );
            }

            if (soft) {
                for (int j = 3; j >= 1; --j) {
                    if (
                        (x1 < x_center ? x_start - j < 0 : x_start - j < x1) // This bounds check is incorrect but is consistent with the original shader
                            || (x2 < x_center ? x_start - j >= x2 : x_start - j >= w) // This bounds check is incorrect but is consistent with the original shader
                    ) {
                        continue;
                    }
                    uint32_t *pixel = shadowbuffer + (size_t)w * (size_t)i + (size_t)x_start - (size_t)j;
                    for (size_t k = 0; k < 3; ++k) {
                        ((uint8_t *)pixel)[k] = std::lround((double)((uint8_t *)pixel)[k] * ((double)j / (double)4));
                    }
                }

                if (x2 != x_center) {
                    for (int j = 1; j <= 3; ++j) {
                        if (
                            (x1 < x_center ? x_end < -j : x_end < x1 - j) // This bounds check is incorrect but is consistent with the original shader
                                || (x2 < x_center ? x_end >= x2 - j : x_end >= w - j) // This bounds check is incorrect but is consistent with the original shader
                        ) {
                            continue;
                        }
                        uint32_t *pixel = shadowbuffer + (size_t)w * (size_t)i + (size_t)x_end + (size_t)j;
                        for (size_t k = 0; k < 3; ++k) {
                            ((uint8_t *)pixel)[k] = std::lround((double)((uint8_t *)pixel)[k] * ((double)j / (double)4));
                        }
                    }
                }
            }
        }
    } else {
        TEXFBO newTex = shState->texPool().request(width(), height());

        FloatRect texRect(rect());

        Quad &quad = shState->gpQuad();
        quad.setTexPosRect(texRect, texRect);
        quad.setColor(Vec4(1, 1, 1, 1));

        KglShadowShaderH &shader = shState->shaders().kglShadowH;
        shader.bind();
        shader.setParams(x1, x2, y, soft, w, h, x_center, y_center, slope1, slope2);

        FBO::bind(newTex.fbo);
        p->pushSetViewport(shader);
        p->bindTexture(shader, false);

        p->blitQuad(quad);

        p->popViewport();

        TEX::unbind();

        shState->texPool().release(p->gl);
        p->gl = newTex;
    }

    p->onModified();

    return 1;
}

int Bitmap::kglShadowShaderV(int y1, int y2, int x, bool wall, bool soft)
{
    guardDisposed();
    /* No CPU version of this operation. */
    cpuPromote(p);
    GUARD_ANIMATED;

    if (hasHires()) {
        return p->selfHires->kglShadowShaderV(y1 * p->selfHires->height() / height(), y2 * p->selfHires->height() / height(), x * p->selfHires->width() / width(), wall, soft);
    }

    int w = width();
    int h = height();

    if (x < 0 || x >= h || x == w / 2) {
        return 111;
    }

    if (w <= 0 || h <= 0) {
        return 1;
    }

    y1 = clamp(y1, 0, h - 1);
    y2 = clamp(y2, 0, h - 1);

    int x_center = w / 2;
    int y_center = h / 2;

    double slope1 = (double)(y1 - y_center) / (double)(x - x_center);
    double slope2 = (double)(y2 - y_center) / (double)(x - x_center);

    if (isMega()) {
        uint32_t *shadowbuffer = (uint32_t *)p->megaSurface->pixels;

        int x_start, x_end;
        if (x < x_center) {
            x_start = 0;
            x_end = x - 1;
        } else {
            x_start = x;
            x_end = w - 1;
        }

        for (int i = x_start; i <= x_end; ++i) {
            double y_start_raw = std::round(slope1 * (double)(i - x_center) + (double)y_center);
            double y_end_raw = std::round(slope2 * (double)(i - x_center) + (double)y_center + 0.2f); // The original shader contains a +0.2 adjustment factor for some reason

            int y_start = wall && y1 >= y_center ? y1 : (int)clamp(y_start_raw, 0., (double)h + 3.);
            int y_end = wall && y2 >= y_center ? y2 - 1 : (int)clamp(y_end_raw, -4., y2 < y_center ? (double)y2 - 1. : (double)h - 1.); // This bounds check is incorrect but is consistent with the original shader

            if (y_start <= y_end) {
                for (int j = y_start; j <= y_end; ++j) {
                    shadowbuffer[(size_t)w * (size_t)j + (size_t)i] = 0;
                }
            }

            if (soft) {
                if (!wall || y1 < y_center) {
                    for (int j = 3; j >= 1; --j) {
                        if (
                            (y1 < y_center ? y_start - j < 0 : y_start - j < y1) // This bounds check is incorrect but is consistent with the original shader
                                || (y2 < y_center ? y_start - j >= y2 : y_start - j >= h) // This bounds check is incorrect but is consistent with the original shader
                        ) {
                            continue;
                        }
                        uint32_t *pixel = shadowbuffer + (size_t)w * ((size_t)y_start - (size_t)j) + (size_t)i;
                        for (size_t k = 0; k < 3; ++k) {
                            ((uint8_t *)pixel)[k] = std::lround((double)((uint8_t *)pixel)[k] * ((double)j / (double)4));
                        }
                    }
                }

                if (y2 != y_center && (!wall || y2 < y_center)) {
                    for (int j = 1; j <= 3; ++j) {
                        if (
                            (y1 < y_center ? y_end < -j : y_end < y1 - j) // This bounds check is incorrect but is consistent with the original shader
                                || (y2 < y_center ? y_end >= y2 - j : y_end >= h - j) // This bounds check is incorrect but is consistent with the original shader
                        ) {
                            continue;
                        }
                        uint32_t *pixel = shadowbuffer + (size_t)w * ((size_t)y_end + (size_t)j) + (size_t)i;
                        for (size_t k = 0; k < 3; ++k) {
                            ((uint8_t *)pixel)[k] = std::lround((double)((uint8_t *)pixel)[k] * ((double)j / (double)4));
                        }
                    }
                }
            }
        }
    } else {
        TEXFBO newTex = shState->texPool().request(width(), height());

        FloatRect texRect(rect());

        Quad &quad = shState->gpQuad();
        quad.setTexPosRect(texRect, texRect);
        quad.setColor(Vec4(1, 1, 1, 1));

        KglShadowShaderV &shader = shState->shaders().kglShadowV;
        shader.bind();
        shader.setParams(y1, y2, x, wall, soft, w, h, x_center, y_center, slope1, slope2);

        FBO::bind(newTex.fbo);
        p->pushSetViewport(shader);
        p->bindTexture(shader, false);

        p->blitQuad(quad);

        p->popViewport();

        TEX::unbind();

        shState->texPool().release(p->gl);
        p->gl = newTex;
    }

    p->onModified();

    return 1;
}

void Bitmap::bindTex(ShaderBase &shader, bool substituteLoresSize)
{
    // Hires mode is handled by p->bindTexture.

    p->bindTexture(shader, substituteLoresSize);
}

void Bitmap::taintArea(const IntRect &rect)
{
    if (hasHires()) {
        int destX, destY, destWidth, destHeight;
        destX = rect.x * p->selfHires->width() / width();
        destY = rect.y * p->selfHires->height() / height();
        destWidth = rect.w * p->selfHires->width() / width();
        destHeight = rect.h * p->selfHires->height() / height();

        p->selfHires->taintArea(IntRect(destX, destY, destWidth, destHeight));
    }

    p->addTaintedArea(rect);
}


/* Called when an image could not be allocated: give back every pixel copy
 * that can be made again, and move CPU-composed bitmaps to the GPU. */
void Bitmap::reclaimMemory()
{
    textCacheClear();
    dropAllReadbacks();
    try
    {
        while (!cpuBitmapList.empty())
            cpuPromote(cpuBitmapList.front());
    }
    catch (const Exception &)
    {
        /* Graphics memory is short too; keep the rest where they are. */
    }
}

/* Once per frame. A CPU-composed bitmap that has been drawn keeps two copies:
 * its pixels here and the texture they were uploaded to. The pixels only
 * matter while the game keeps writing to it. A game that composes thousands
 * of small bitmaps once and then just shows them (To the Moon) fills the C
 * heap with those copies: 56 MiB of them left no room for anything else.
 *
 * So when the small ones, which are the ones in the heap, pass 12 MiB, those
 * not written for five seconds are handed to the GPU, which frees their
 * pixels. Large bitmaps are left alone: they do not weigh on the heap, and
 * handing over a full-screen one costs a visible hitch (measured in a BLACK
 * SOULS battle). A few per frame, so it never shows as a stall. */
void Bitmap::frameTick()
{
    ++cpuFrame;
    if (cpuHeapBytes <= 12u * 1024 * 1024)
        return;
    int promoted = 0;
    for (int looked = 0; looked < 24 && promoted < 3 && !cpuBitmapList.empty(); ++looked)
    {
        BitmapPrivate *oldest = cpuBitmapList.front();
        const size_t bytes = (size_t)oldest->cpu->w * oldest->cpu->h * 4;
        /* Large, still being written, never shown, or changed since the last
         * upload: nothing to gain now; look again after the others. */
        if (bytes >= cpuHeapResident || cpuFrame - oldest->cpuStamp <= 300 ||
            oldest->gl.tex == TEX::ID(0) || oldest->cpuDirty)
        {
            cpuBitmapList.splice(cpuBitmapList.end(), cpuBitmapList, oldest->cpuIter);
            continue;
        }
        try
        {
            cpuPromote(oldest);
            ++promoted;
        }
        catch (const Exception &)
        {
            /* Graphics memory is short; leave things as they are. */
            return;
        }
    }
}

/* True when CPU-composed bitmaps hold most of their budget. Bitmaps a game
 * dropped without disposing keep their pixels until Ruby collects them, so
 * the binding collects when this turns true. */
bool Bitmap::cpuPixelsNearBudget()
{
    return cpuBitmapBytes > cpuBitmapBudget / 4 * 3;
}

size_t Bitmap::cpuPixelBytes()
{
    return cpuBitmapBytes;
}

/* For the memory snapshot: CPU-composed bitmaps (count, bytes), read-back
 * copies (bytes) and cached text (bytes). */
extern "C" void vitaBitmapMemoryFigures(size_t figures[4])
{
    figures[0] = cpuBitmapList.size();
    figures[1] = cpuBitmapBytes;
    figures[2] = readbackBytes;
    figures[3] = textCacheBytes;
}

int Bitmap::maxSize(){
    return glState.caps.maxTexSize;
}

void Bitmap::assumeRubyGC()
{
    p->assumingRubyGC = true;
}

void Bitmap::releaseResources()
{
    /* Disposal drops a pending clear: no pixels will ever be observed. */
    if (p && p->pendingClear)
    {
        p->pendingClear = false;
        FrameProfile::Scope discarded(FrameProfile::ClearDiscarded);
    }
    if (p->selfHires && !p->assumingRubyGC) {
        delete p->selfHires;
    }

    if (p->megaSurface)
    {
        vitaDiagLog("BITMAP", "mega_surface_free_begin self=%p surface=%p",
                    (void *)this, (void *)p->megaSurface);
        SDL_FreeSurface(p->megaSurface);
        vitaDiagLog("BITMAP", "mega_surface_free_done self=%p", (void *)this);
    }
    else if (p->animation.enabled) {
        p->animation.enabled = false;
        p->animation.playing = false;
        for (TEXFBO &tex : p->animation.frames)
            shState->texPool().release(tex);
    }
    else if (p->cpu)
    {
        /* No framebuffer was ever made; the pixels go with the private data. */
        if (p->gl.tex != TEX::ID(0))
            TEX::del(p->gl.tex);
    }
    else
        shState->texPool().release(p->gl);
    
    delete p;
}

void Bitmap::loresDisposal()
{
    loresDispCon.disconnect();
    dispose();
}
