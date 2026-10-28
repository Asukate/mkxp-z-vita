#!/usr/bin/env python3
"""Exercise the production CPU bitmap raster helpers from src/display/bitmap.cpp.

The blend is compared with the bitmapBlit shader's formula computed in floating
point, and every blit, fill and gradient runs under the address sanitizer with
rects that hang over each edge of both surfaces.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SOURCE = (ROOT / 'src/display/bitmap.cpp').read_text()


def function(name):
    """Return the full text of one static helper, found by its name."""
    start = re.search(r'^static [^\n;(]*\b' + re.escape(name) + r'\(', SOURCE, re.M)
    assert start, name
    depth, position = 0, SOURCE.index('{', start.start())
    while True:
        if SOURCE[position] == '{':
            depth += 1
        elif SOURCE[position] == '}':
            depth -= 1
            if depth == 0:
                return SOURCE[start.start():position + 1] + '\n'
        position += 1


PREAMBLE = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
struct Vec4 { float x, y, z, w; Vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {} };
struct IntRect { int x, y, w, h; IntRect() : x(0), y(0), w(0), h(0) {} IntRect(int a, int b, int c, int d) : x(a), y(b), w(c), h(d) {} };
template<class T> static T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
struct SDL_Surface { int w, h, pitch; void *pixels; };
// Exact-size heap blocks: the sanitizer reports any write or read outside them.
static SDL_Surface *surface(int w, int h) {
    SDL_Surface *s = new SDL_Surface{w, h, w * 4, std::malloc((size_t)w * h * 4)};
    return s;
}
static void release(SDL_Surface *s) { std::free(s->pixels); delete s; }
static uint32_t &at(SDL_Surface *s, int x, int y) { return ((uint32_t *)s->pixels)[y * s->w + x]; }
'''

CHECKS = r'''
static void expect(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }

// bitmapBlit.frag: a = sa*o + da(1 - sa*o); rgb = (src*sa*o + dst*da(1 - sa*o)) / a, src rgb when a is 0.
static uint32_t reference(uint32_t d, uint32_t s, int opacity) {
    const double co1 = ((s >> 24) / 255.0) * (opacity / 255.0);
    const double co2 = ((d >> 24) / 255.0) * (1.0 - co1);
    const double a = co1 + co2;
    uint32_t out = (uint32_t)std::lround(a * 255.0) << 24;
    for (int shift = 0; shift < 24; shift += 8) {
        const double sc = ((s >> shift) & 255) / 255.0, dc = ((d >> shift) & 255) / 255.0;
        const double c = a == 0.0 ? sc : (co1 * sc + co2 * dc) / a;
        out |= (uint32_t)std::lround(c * 255.0) << shift;
    }
    return out;
}
static int worst(uint32_t a, uint32_t b) {
    int w = 0;
    for (int shift = 0; shift < 32; shift += 8)
        w = std::max(w, std::abs((int)((a >> shift) & 255) - (int)((b >> shift) & 255)));
    return w;
}

// The scaled path before columns were precomputed and 1x1 sources became a
// fill (kept verbatim): the new code must give the same pixels.
static void oldScaled(SDL_Surface *dst, const IntRect &destRect, SDL_Surface *src,
                      const IntRect &sourceRect, int opacity, bool smooth) {
    const uint32_t *recip = cpuReciprocals();
    const int x0 = std::max(destRect.x, 0), y0 = std::max(destRect.y, 0);
    const int x1 = std::min(destRect.x + destRect.w, dst->w);
    const int y1 = std::min(destRect.y + destRect.h, dst->h);
    if (x1 <= x0 || y1 <= y0 || src->w <= 0 || src->h <= 0) return;
    const int sx0 = clamp(sourceRect.x, 0, src->w - 1), sy0 = clamp(sourceRect.y, 0, src->h - 1);
    const int sx1 = clamp(sourceRect.x + sourceRect.w - 1, sx0, src->w - 1);
    const int sy1 = clamp(sourceRect.y + sourceRect.h - 1, sy0, src->h - 1);
    const uint8_t *sp = (const uint8_t *)src->pixels;
    uint8_t *dp = (uint8_t *)dst->pixels;
    const int64_t stepX = ((int64_t)sourceRect.w << 16) / destRect.w;
    const int64_t stepY = ((int64_t)sourceRect.h << 16) / destRect.h;
    for (int y = y0; y < y1; ++y) {
        int64_t v = (int64_t)(y - destRect.y) * stepY + stepY / 2 - (smooth ? 0x8000 : 0);
        if (v < 0) v = 0;
        int sy = sourceRect.y + (int)(v >> 16);
        const uint32_t fy = smooth ? (uint32_t)((v & 0xFFFF) >> 8) : 0;
        sy = clamp(sy, sy0, sy1);
        const int syn = std::min(sy + 1, sy1);
        const uint32_t *srow = (const uint32_t *)(sp + (size_t)sy * src->pitch);
        const uint32_t *srown = (const uint32_t *)(sp + (size_t)syn * src->pitch);
        uint32_t *drow = (uint32_t *)(dp + (size_t)y * dst->pitch);
        for (int x = x0; x < x1; ++x) {
            int64_t u = (int64_t)(x - destRect.x) * stepX + stepX / 2 - (smooth ? 0x8000 : 0);
            if (u < 0) u = 0;
            int sx = clamp(sourceRect.x + (int)(u >> 16), sx0, sx1);
            uint32_t s;
            if (smooth) {
                const uint32_t fx = (uint32_t)((u & 0xFFFF) >> 8);
                const int sxn = std::min(sx + 1, sx1);
                s = cpuLerp(cpuLerp(srow[sx], srow[sxn], fx), cpuLerp(srown[sx], srown[sxn], fx), fy);
            } else
                s = srow[sx];
            drow[x] = cpuOver(drow[x], s, opacity, recip);
        }
    }
}
static uint32_t rnd() { static uint32_t x = 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }

int main() try {
    const uint32_t *recip = cpuReciprocals();

    // Blend against the shader formula. Colour under a fully transparent result is
    // never visible, and the 8-bit intermediate alpha allows one level of rounding;
    // nearly transparent results amplify that in colour, as they do on the GPU.
    for (int i = 0; i < 400000; ++i) {
        uint32_t d = rnd(), s = rnd(); int opacity = i % 5 == 0 ? 255 : (int)(rnd() % 256);
        if (i % 7 == 0) d |= 0xFF000000u;
        if (i % 11 == 0) s |= 0xFF000000u;
        if (i % 13 == 0) d &= 0x00FFFFFFu;
        const uint32_t got = cpuOver(d, s, (uint32_t)opacity, recip), want = reference(d, s, opacity);
        expect(std::abs((int)(got >> 24) - (int)(want >> 24)) <= 1, "blend alpha differs from the shader formula");
        if ((want >> 24) >= 32)
            expect(worst(got & 0xFFFFFF, want & 0xFFFFFF) <= 3, "blend colour differs from the shader formula");
    }
    expect(cpuOver(0x80112233u, 0x00FFFFFFu, 255, recip) == 0x80112233u, "transparent source changed the destination");
    expect(cpuOver(0x80112233u, 0xFF445566u, 255, recip) == 0xFF445566u, "opaque source did not replace");
    expect(cpuOver(0x00112233u, 0x40445566u, 255, recip) == 0x40445566u, "source over nothing is not a copy");

    // Unscaled blits with every kind of overhang; untouched pixels must stay untouched.
    for (int round = 0; round < 4000; ++round) {
        const int dw = 1 + rnd() % 40, dh = 1 + rnd() % 20, sw = 1 + rnd() % 40, sh = 1 + rnd() % 20;
        SDL_Surface *dst = surface(dw, dh), *src = surface(sw, sh);
        for (int i = 0; i < dw * dh; ++i) ((uint32_t *)dst->pixels)[i] = rnd();
        // Every other round mostly opaque runs, the shape of walls and tiles;
        // one round in four also has clear runs (alpha 0, any colour).
        for (int i = 0; i < sw * sh; ++i)
            ((uint32_t *)src->pixels)[i] = (round & 3) == 3 ? (rnd() % 3 ? rnd() | 0xFF000000u : rnd() & 0x00FFFFFFu)
                                         : (round & 1) && rnd() % 8 ? rnd() | 0xFF000000u : rnd();
        std::vector<uint32_t> before((uint32_t *)dst->pixels, (uint32_t *)dst->pixels + dw * dh);
        const int w = 1 + rnd() % 50, h = 1 + rnd() % 30, opacity = round % 3 ? 255 : (int)(rnd() % 256);
        const IntRect to((int)(rnd() % 80) - 40, (int)(rnd() % 50) - 25, w, h);
        const IntRect from((int)(rnd() % 80) - 40, (int)(rnd() % 50) - 25, w, h);
        cpuBlit(dst, to, src, from, opacity, false);
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {
                const int sx = from.x + (x - to.x), sy = from.y + (y - to.y);
                const bool covered = x >= to.x && x < to.x + w && y >= to.y && y < to.y + h &&
                                     sx >= 0 && sx < sw && sy >= 0 && sy < sh;
                const uint32_t want = covered ? cpuOver(before[y * dw + x], at(src, sx, sy), (uint32_t)opacity, recip)
                                              : before[y * dw + x];
                expect(at(dst, x, y) == want, "unscaled blit wrote the wrong pixel or outside its rect");
            }
        release(dst); release(src);
    }

    // Scaled blits: only pixels inside the destination rect may change, with and
    // without filtering; a uniform source must stay uniform.
    for (int round = 0; round < 3000; ++round) {
        const int dw = 1 + rnd() % 48, dh = 1 + rnd() % 24, sw = 1 + rnd() % 48, sh = 1 + rnd() % 24;
        SDL_Surface *dst = surface(dw, dh), *src = surface(sw, sh);
        const uint32_t colour = rnd() | 0xFF000000u;
        std::memset(dst->pixels, 0, (size_t)dw * dh * 4);
        for (int i = 0; i < sw * sh; ++i) ((uint32_t *)src->pixels)[i] = colour;
        const IntRect to((int)(rnd() % 60) - 20, (int)(rnd() % 40) - 12, 1 + rnd() % 70, 1 + rnd() % 40);
        const IntRect from(rnd() % sw, rnd() % sh, 1, 1);
        IntRect part(from.x, from.y, 1 + rnd() % (sw - from.x), 1 + rnd() % (sh - from.y));
        cpuBlit(dst, to, src, part, 255, round & 1);
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {
                const bool inside = x >= to.x && x < to.x + to.w && y >= to.y && y < to.y + to.h;
                expect(at(dst, x, y) == (inside ? colour : 0u), "scaled blit changed a pixel outside its rect or bent a flat colour");
            }
        release(dst); release(src);
    }


    // Scaled blits, including a stretched 1x1 tint, give exactly the old pixels
    // over opaque, empty and half-transparent destinations.
    for (int round = 0; round < 6000; ++round) {
        const int dw = 1 + rnd() % 48, dh = 1 + rnd() % 24, sw = 1 + rnd() % 24, sh = 1 + rnd() % 12;
        SDL_Surface *a = surface(dw, dh), *b = surface(dw, dh), *src = surface(sw, sh);
        // One round in five draws over three colours only, so the tint's
        // reuse of the previous pixel's blend is exercised.
        const uint32_t few[3] = {rnd() | 0xFF000000u, rnd(), rnd() & 0x00FFFFFFu};
        for (int i = 0; i < dw * dh; ++i) {
            uint32_t d = rnd();
            switch (rnd() % 3) { case 0: d |= 0xFF000000u; break; case 1: d &= 0x00FFFFFFu; break; default: break; }
            if (round % 5 == 4) d = few[rnd() % 3];
            ((uint32_t *)a->pixels)[i] = ((uint32_t *)b->pixels)[i] = d;
        }
        for (int i = 0; i < sw * sh; ++i) ((uint32_t *)src->pixels)[i] = rnd() % 4 ? rnd() : rnd() | 0xFF000000u;
        IntRect from(rnd() % sw, rnd() % sh, 1, 1);
        if (round % 3) { from.w = 1 + rnd() % (sw - from.x); from.h = 1 + rnd() % (sh - from.y); }
        IntRect to((int)(rnd() % 60) - 20, (int)(rnd() % 40) - 12, 1 + rnd() % 70, 1 + rnd() % 40);
        if (to.w == from.w && to.h == from.h) to.w += 1;
        const int opacity = round % 4 ? 255 : (int)(rnd() % 256);
        const bool smooth = round & 1;
        cpuBlit(a, to, src, from, opacity, smooth);
        oldScaled(b, to, src, from, opacity, smooth);
        expect(std::memcmp(a->pixels, b->pixels, size_t(dw) * dh * 4) == 0, "scaled blit differs from the previous code");
        release(a); release(b); release(src);
    }

    // Fill and gradient stay inside the clipped rect; the ramp runs from the first colour to the second.
    SDL_Surface *s = surface(32, 8);
    std::memset(s->pixels, 0, 32 * 8 * 4);
    cpuFill(s, IntRect(3, 2, 10, 4), 0xAABBCCDDu);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 32; ++x)
            expect(at(s, x, y) == ((x >= 3 && x < 13 && y >= 2 && y < 6) ? 0xAABBCCDDu : 0u), "fill left its rect");
    cpuGradient(s, IntRect(0, 0, 32, 8), IntRect(0, 0, 32, 8), Vec4(0, 0, 0, 1), Vec4(1, 1, 1, 1), false);
    for (int x = 1; x < 32; ++x)
        expect((at(s, x, 0) & 255) >= (at(s, x - 1, 0) & 255), "horizontal gradient is not monotonic");
    expect((at(s, 0, 0) & 255) <= 8 && (at(s, 31, 7) & 255) >= 247, "gradient does not span its two colours");
    expect(cpuPack(Vec4(1, 0, 0.5f, 1)) == 0xFF8000FFu, "colour packing order or rounding changed");
    release(s);

    // The row-copy gradient is pixel-identical to packing every pixel (the previous code).
    for (int round = 0; round < 300; ++round) {
        const int w = 1 + rnd() % 70, h = 1 + rnd() % 30;
        SDL_Surface *fast = surface(w, h), *slow = surface(w, h);
        std::memset(fast->pixels, 0x5A, size_t(w) * h * 4);
        std::memset(slow->pixels, 0x5A, size_t(w) * h * 4);
        const IntRect whole(int(rnd() % 20) - 10, int(rnd() % 10) - 5, 1 + rnd() % 90, 1 + rnd() % 40);
        IntRect clip(std::max(0, whole.x), std::max(0, whole.y), 0, 0);
        clip.w = std::min(w, whole.x + whole.w) - clip.x;
        clip.h = std::min(h, whole.y + whole.h) - clip.y;
        const Vec4 a(rnd() % 256 / 255.f, rnd() % 256 / 255.f, rnd() % 256 / 255.f, rnd() % 256 / 255.f);
        const Vec4 b(rnd() % 256 / 255.f, rnd() % 256 / 255.f, rnd() % 256 / 255.f, rnd() % 256 / 255.f);
        const bool vertical = round & 1;
        cpuGradient(fast, whole, clip, a, b, vertical);
        if (clip.w > 0 && clip.h > 0) {
            const int extent = vertical ? whole.h : whole.w;
            for (int y = clip.y; y < clip.y + clip.h; ++y)
                for (int x = clip.x; x < clip.x + clip.w; ++x) {
                    const float t = ((vertical ? y - whole.y : x - whole.x) + 0.5f) / extent;
                    ((uint32_t *)((uint8_t *)slow->pixels + size_t(y) * slow->pitch))[x] =
                        cpuPack(Vec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                     a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
                }
        }
        expect(std::memcmp(fast->pixels, slow->pixels, size_t(w) * h * 4) == 0, "gradient differs from the per-pixel ramp");
        release(fast); release(slow);
    }

    std::puts("PASS: blend matches the blit shader formula; unscaled and scaled blits, fills and gradients stay in bounds");
    return 0;
} catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
'''

with tempfile.TemporaryDirectory(prefix='hardrpg-cpu-compose-') as directory:
    source = Path(directory) / 'compose.cpp'
    helpers = ''.join(function(name) for name in
                      ('cpuPack', 'cpuFill', 'cpuGradientAt', 'cpuGradient', 'cpuReciprocals', 'cpuOver', 'cpuLerp', 'cpuBlendConstant', 'cpuBlit'))
    source.write_text(PREAMBLE + helpers + CHECKS)
    binary = Path(directory) / 'compose'
    subprocess.run(['c++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
