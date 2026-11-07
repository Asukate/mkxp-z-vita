#pragma once

#include <cstdint>
#include <arm_neon.h>

// Exact fast path for little-endian RGBA8 source-over on opaque destination
// pixels. Stops at the first group that needs the general straight-alpha blend.
static inline int cpuBlitOpaquePrefix(uint32_t *out, const uint32_t *in, int count, int opacity)
{
    const uint8x8_t opaque = vdup_n_u8(255);
    const uint8x8_t global = vdup_n_u8(uint8_t(opacity));
    int i = 0;
    for (; i + 8 <= count; i += 8)
    {
        const auto src = vld4_u8(reinterpret_cast<const uint8_t *>(in + i));
        const uint64_t alpha = vget_lane_u64(vreinterpret_u64_u8(src.val[3]), 0);
        if (!alpha)
            continue;
        if (opacity == 255 && alpha == UINT64_MAX)
        {
            vst4_u8(reinterpret_cast<uint8_t *>(out + i), src);
            continue;
        }
        auto dst = vld4_u8(reinterpret_cast<const uint8_t *>(out + i));
        if (vget_lane_u64(vreinterpret_u64_u8(dst.val[3]), 0) != UINT64_MAX)
            break;

        uint8x8_t weight = src.val[3];
        if (opacity != 255)
        {
            // Round (source alpha * opacity) / 255, as in cpuOver.
            const auto biased = vaddq_u16(vmull_u8(weight, global), vdupq_n_u16(128));
            weight = vshrn_n_u16(vaddq_u16(biased, vshrq_n_u16(biased, 8)), 8);
        }
        const auto remaining = vsub_u8(opaque, weight);
        for (int channel = 0; channel < 3; ++channel)
        {
            const auto numerator = vmlal_u8(vmull_u8(dst.val[channel], remaining), src.val[channel], weight);
            // cpuOver uses reciprocal[255] == 257. Splitting 257 as 256 + 1
            // makes the rounded 16.16 product exact without widening.
            dst.val[channel] = vrshrn_n_u16(vaddq_u16(numerator, vshrq_n_u16(numerator, 8)), 8);
        }
        vst4_u8(reinterpret_cast<uint8_t *>(out + i), dst);
    }
    return i;
}
