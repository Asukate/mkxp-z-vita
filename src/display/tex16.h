/* HardRPG - dithered 16-bit storage for decoded game images
 * Copyright (C) 2026 Asukate
 *
 * This file is part of HardRPG, free software under the GNU
 * General Public License v3. See LICENSES/GPL-3.0.txt at the repository root.
 *
 * Same packing as the MV/MZ runtime's tex16.h, for straight (not
 * premultiplied) RGBA: colour channels are kept under any alpha, so there is
 * no rgb <= alpha clamp and fully transparent 5551 pixels keep their colour.
 *
 * Formats follow the GL packed layouts vitaGL stores as-is (red in the top
 * bits): RGB565 for fully opaque images, RGBA5551 when alpha is only 0 or
 * 255, RGBA4444 when there is any partial alpha. Colours get a 4x4 ordered
 * (Bayer) dither on absolute pixel coordinates, so banding turns into a fine
 * pattern that the Vita screen hides. */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace tex16 {

enum : uint8_t { kRgba8 = 0, k565 = 1, k5551 = 2, k4444 = 3 };

inline unsigned bytesPerPixel(uint8_t fmt) { return fmt == kRgba8 ? 4u : 2u; }

// floor(v * L / 255 + (t + 0.5) / 16) for v in 0..255, t in 0..15, exactly
// (checked against the float formula for L = 15, 31, 63). Pure 16-bit math,
// so NEON can do 8 lanes at once.
inline unsigned quant(unsigned v, unsigned L, unsigned t) {
    const unsigned vl = v * L;
    return (vl + ((vl + (vl >> 8) + 1) >> 8) + 16 * t + 8) >> 8;
}

static constexpr uint8_t kBayer[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Plain C++ versions (also the reference for the NEON ones).
inline uint8_t classifyScalar(const uint8_t *rgba, size_t count) {
    bool binary = true;
    const uint8_t *a = rgba + 3;
    for (size_t i = 0; i < count; ++i, a += 4) {
        const uint8_t v = *a;
        if (v == 255) continue;
        if (v != 0) return k4444;      // partial alpha somewhere
        binary = false;                 // has fully transparent pixels
    }
    return binary ? k565 : k5551;
}

inline uint16_t packPixel(const uint8_t *p, int t, uint8_t fmt) {
    const unsigned R = p[0], G = p[1], B = p[2], A = p[3];
    if (fmt == k565)
        return (uint16_t)((quant(R, 31, t) << 11) | (quant(G, 63, t) << 5) | quant(B, 31, t));
    if (fmt == k5551)
        return (uint16_t)((quant(R, 31, t) << 11) | (quant(G, 31, t) << 6) |
                           (quant(B, 31, t) << 1) | (A >= 128 ? 1u : 0u));
    const unsigned a4 = quant(A, 15, t);
    const unsigned r4 = quant(R, 15, t), g4 = quant(G, 15, t), b4 = quant(B, 15, t);
    return (uint16_t)((r4 << 12) | (g4 << 8) | (b4 << 4) | a4);
}

// Packs `rows` rows of `width` RGBA pixels into 16-bit pixels. `y0` is the
// first row's position in the image (keeps the dither pattern continuous
// across bands). `dst` may be the same buffer as `src` (in-place packing):
// output never overtakes unread input (pixel i writes bytes 2i..2i+1).
inline void packScalar(const uint8_t *src, uint8_t *dst, int width, int rows, int y0,
                       uint8_t fmt) {
    for (int r = 0; r < rows; ++r) {
        const uint8_t *bay = kBayer[(y0 + r) & 3];
        for (int x = 0; x < width; ++x) {
            const uint16_t v = packPixel(src, bay[x & 3], fmt);
            src += 4;
            dst[0] = (uint8_t)(v & 0xFF);   // little-endian, as the GPU reads it
            dst[1] = (uint8_t)(v >> 8);
            dst += 2;
        }
    }
}

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
inline uint16x8_t quantN(uint8x8_t v, uint8_t L, uint16x8_t dith) {
    const uint16x8_t vl = vmull_u8(v, vdup_n_u8(L));
    const uint16x8_t corr = vshrq_n_u16(
        vaddq_u16(vaddq_u16(vl, vshrq_n_u16(vl, 8)), vdupq_n_u16(1)), 8);
    return vshrq_n_u16(vaddq_u16(vaddq_u16(vl, corr), dith), 8);
}

inline uint8_t classifyNeon(const uint8_t *rgba, size_t count) {
    size_t i = 0;
    uint8x16_t mn = vdupq_n_u8(255);
    while (i + 16 <= count) {
        uint8x16_t partial = vdupq_n_u8(0);
        const size_t stop = (count - i) / 16 > 64 ? i + 16 * 64 : count - (count - i) % 16;
        for (; i < stop; i += 16) {
            const uint8x16_t a = vld4q_u8(rgba + i * 4).val[3];
            mn = vminq_u8(mn, a);
            // a - 1 wraps 0 -> 255 and 255 -> 254: below 254 means 1..254.
            partial = vorrq_u8(partial, vcltq_u8(vsubq_u8(a, vdupq_n_u8(1)), vdupq_n_u8(254)));
        }
        const uint64x2_t p = vreinterpretq_u64_u8(partial);
        if (vgetq_lane_u64(p, 0) | vgetq_lane_u64(p, 1)) return k4444;
    }
    uint8_t tail = classifyScalar(rgba + i * 4, count - i);
    if (tail == k4444) return k4444;
    const uint8x8_t m8 = vmin_u8(vget_low_u8(mn), vget_high_u8(mn));
    const uint8x8_t m4 = vpmin_u8(m8, m8), m2 = vpmin_u8(m4, m4), m1 = vpmin_u8(m2, m2);
    const bool allOpaque = vget_lane_u8(m1, 0) == 255 && tail == k565;
    return allOpaque ? k565 : k5551;
}

inline void packNeon(const uint8_t *src, uint8_t *dst, int width, int rows, int y0,
                     uint8_t fmt) {
    for (int r = 0; r < rows; ++r) {
        const uint8_t *bay = kBayer[(y0 + r) & 3];
        const uint16_t d[8] = {
            (uint16_t)(16 * bay[0] + 8), (uint16_t)(16 * bay[1] + 8),
            (uint16_t)(16 * bay[2] + 8), (uint16_t)(16 * bay[3] + 8),
            (uint16_t)(16 * bay[0] + 8), (uint16_t)(16 * bay[1] + 8),
            (uint16_t)(16 * bay[2] + 8), (uint16_t)(16 * bay[3] + 8)};
        const uint16x8_t dith = vld1q_u16(d);
        int x = 0;
        for (; x + 8 <= width; x += 8) {
            const uint8x8x4_t p = vld4_u8(src);
            uint16x8_t v;
            if (fmt == k565) {
                v = vorrq_u16(vorrq_u16(vshlq_n_u16(quantN(p.val[0], 31, dith), 11),
                                        vshlq_n_u16(quantN(p.val[1], 63, dith), 5)),
                              quantN(p.val[2], 31, dith));
            } else if (fmt == k5551) {
                v = vorrq_u16(vorrq_u16(vshlq_n_u16(quantN(p.val[0], 31, dith), 11),
                                        vshlq_n_u16(quantN(p.val[1], 31, dith), 6)),
                              vshlq_n_u16(quantN(p.val[2], 31, dith), 1));
                const uint16x8_t opaque = vshrq_n_u16(vmovl_u8(p.val[3]), 7);
                v = vorrq_u16(v, opaque);
            } else {
                const uint16x8_t a4 = quantN(p.val[3], 15, dith);
                const uint16x8_t r4 = quantN(p.val[0], 15, dith);
                const uint16x8_t g4 = quantN(p.val[1], 15, dith);
                const uint16x8_t b4 = quantN(p.val[2], 15, dith);
                v = vorrq_u16(vorrq_u16(vshlq_n_u16(r4, 12), vshlq_n_u16(g4, 8)),
                              vorrq_u16(vshlq_n_u16(b4, 4), a4));
            }
            // Byte store: no alignment needs; read of these 32 bytes is done.
            vst1q_u8(dst, vreinterpretq_u8_u16(v));
            src += 32;
            dst += 16;
        }
        for (; x < width; ++x) {
            const uint16_t v = packPixel(src, bay[x & 3], fmt);
            src += 4;
            dst[0] = (uint8_t)(v & 0xFF);
            dst[1] = (uint8_t)(v >> 8);
            dst += 2;
        }
    }
}

// Runs once: NEON must match the plain version bit for bit, or it is not used.
inline bool neonVerified() {
    static int ok = -1;
    if (ok < 0) {
        const int W = 61, H = 9;   // odd width: exercises the tail loop too
        uint8_t px[W * H * 4];
        uint32_t seed = 12345;
        ok = 1;
        for (int fmt = k565; fmt <= k4444 && ok; ++fmt) {
            for (int i = 0; i < W * H; ++i) {
                seed = seed * 1103515245u + 12345u;
                const uint8_t a = fmt == k565 ? 255 : fmt == k5551 ? ((seed >> 20) & 1 ? 255 : 0)
                                                                   : (uint8_t)(seed >> 24);
                px[i * 4 + 3] = a;
                for (int k = 0; k < 3; ++k) {
                    seed = seed * 1103515245u + 12345u;
                    px[i * 4 + k] = (uint8_t)(((seed >> 24) * a) / 255);
                }
            }
            uint8_t a[W * H * 2], b[W * H * 2];
            packScalar(px, a, W, H, 3, (uint8_t)fmt);
            packNeon(px, b, W, H, 3, (uint8_t)fmt);
            if (std::memcmp(a, b, sizeof a) != 0) ok = 0;
            if (classifyNeon(px, W * H) != classifyScalar(px, W * H)) ok = 0;
        }
    }
    return ok == 1;
}
#endif

inline uint8_t classify(const uint8_t *rgba, size_t count) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    if (neonVerified()) return classifyNeon(rgba, count);
#endif
    return classifyScalar(rgba, count);
}

inline void pack(const uint8_t *src, uint8_t *dst, int width, int rows, int y0,
                 uint8_t fmt) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    if (neonVerified()) return packNeon(src, dst, width, rows, y0, fmt);
#endif
    packScalar(src, dst, width, rows, y0, fmt);
}

inline bool usingNeon() {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    return neonVerified();
#else
    return false;
#endif
}

// Inverse, for tests and diagnostics: 16-bit pixel back to RGBA8.
inline void unpack(uint16_t v, uint8_t fmt, uint8_t out[4]) {
    auto ex = [](unsigned x, unsigned L) { return (uint8_t)((x * 255u + L / 2) / L); };
    if (fmt == k565) {
        out[0] = ex(v >> 11, 31); out[1] = ex((v >> 5) & 63, 63);
        out[2] = ex(v & 31, 31); out[3] = 255;
    } else if (fmt == k5551) {
        out[0] = ex(v >> 11, 31); out[1] = ex((v >> 6) & 31, 31);
        out[2] = ex((v >> 1) & 31, 31); out[3] = (v & 1) ? 255 : 0;
    } else {
        out[0] = ex(v >> 12, 15); out[1] = ex((v >> 8) & 15, 15);
        out[2] = ex((v >> 4) & 15, 15); out[3] = ex(v & 15, 15);
    }
}

} // namespace tex16
