// Run with the ARMv7 hard-float compiler and qemu-arm -cpu cortex-a9.
#include "display/cpu-blit-neon.h"
#include <cassert>
#include <cstdio>

static uint32_t scalarOpaque(uint32_t dst, uint32_t src, unsigned opacity)
{
    const unsigned alpha = ((src >> 24) * opacity + 127) / 255;
    uint32_t result = 0xff000000;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const unsigned numerator = ((src >> shift) & 255) * alpha +
                                   ((dst >> shift) & 255) * (255 - alpha);
        result |= ((numerator * 257 + 32768) >> 16) << shift;
    }
    return result;
}

int main()
{
    uint32_t src[24], dst[24], expected[24];
    for (unsigned opacity = 0; opacity < 256; ++opacity)
        for (unsigned a = 0; a < 256; ++a) {
            // Test unaligned row starts, variable tails and mixed alpha.
            for (int i = 0; i < 24; ++i) {
                src[i] = (a << 24) | (((a + 171 * i) * 0x13579bu) & 0xffffff);
                dst[i] = 0xff000000 | (((opacity + 21 * i) * 0x314159u) & 0xffffff);
                expected[i] = scalarOpaque(dst[i], src[i], opacity);
            }
            const bool mixed = opacity % 2 && a > 0 && a < 255;
            if (mixed) dst[11] &= 0x7fffffff;
            const int used = cpuBlitOpaquePrefix(dst + 1, src + 1, 23, opacity);
            assert(used == (mixed ? 8 : 16));
            for (int i = 1; i <= used; ++i)
                assert(dst[i] == expected[i]);
            for (int i = used + 1; i < 24; ++i) {
                const uint32_t old = 0xff000000 | (((opacity + 21 * i) * 0x314159u) & 0xffffff);
                assert(dst[i] == (mixed && i == 11 ? old & 0x7fffffff : old));
            }
        }
    // Exercise each possible RGB channel numerator, including rounding ties.
    for (unsigned alpha = 1; alpha < 255; ++alpha)
        for (unsigned d = 0; d < 256; ++d)
            for (unsigned s = 0; s < 256; s += 8) {
                for (unsigned i = 0; i < 8; ++i) {
                    src[i] = (alpha << 24) | ((s + i) * 0x010101);
                    dst[i] = 0xff000000 | (d * 0x010101);
                    expected[i] = scalarOpaque(dst[i], src[i], 255);
                }
                assert(cpuBlitOpaquePrefix(dst, src, 8, 255) == 8);
                for (int i = 0; i < 8; ++i) assert(dst[i] == expected[i]);
            }
    std::puts("PASS: ARMv7 NEON exact blend parity, mixed-alpha fallback and row tails");
}
