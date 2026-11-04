#ifndef MKXP_IMAGE_CROP_H
#define MKXP_IMAGE_CROP_H

#include <algorithm>
#include <cstdint>
#include <cstring>

/* Exact RGBA storage for a large image with a constant transparent surround.
 * The logical image is unchanged, including RGB underneath zero alpha. */
struct ImageCrop
{
    int x = 0, y = 0, width = 0, height = 0;
    int logicalWidth = 0, logicalHeight = 0;
    uint32_t background = 0;
    bool active() const { return width != 0; }
};

inline ImageCrop findImageCrop(const void *pixels, int w, int h, int pitch)
{
    ImageCrop result;
    if (size_t(w) * h < 1024u * 1024u)
        return result;
    const uint32_t background = *static_cast<const uint32_t *>(pixels);
    if (background >> 24)
        return result;
    int left = w, top = h, right = 0, bottom = 0;
    for (int y = 0; y < h; ++y)
    {
        const auto *row = reinterpret_cast<const uint32_t *>(
            static_cast<const uint8_t *>(pixels) + size_t(y) * pitch);
        for (int x = 0; x < w; ++x)
            if (row[x] != background)
            {
                left = std::min(left, x);
                right = std::max(right, x + 1);
                top = std::min(top, y);
                bottom = y + 1;
            }
    }
    if (right == 0)
        left = top = 0, right = bottom = 1;
    else
    {
        /* Identical transparent texels around the retained contents keep
         * nearest and bilinear sampling identical at the crop boundary. */
        left = std::max(0, left - 4);
        top = std::max(0, top - 4);
        right = std::min(w, right + 4);
        bottom = std::min(h, bottom + 4);
    }
    if (size_t(right - left) * (bottom - top) > size_t(w) * h * 3 / 4)
        return result;
    result.x = left; result.y = top;
    result.width = right - left; result.height = bottom - top;
    result.logicalWidth = w; result.logicalHeight = h;
    result.background = background;
    return result;
}

/* Move upwards/leftwards within the decoder's allocation; no second image
 * buffer is needed. The surface is freed normally after the upload. */
inline void packImageCrop(void *pixels, int pitch, const ImageCrop &crop)
{
    auto *bytes = static_cast<uint8_t *>(pixels);
    for (int y = 0; y < crop.height; ++y)
        std::memmove(bytes + size_t(y) * crop.width * 4,
                     bytes + size_t(y + crop.y) * pitch + size_t(crop.x) * 4,
                     size_t(crop.width) * 4);
}

inline uint32_t imageCropPixel(const ImageCrop &crop, const void *pixels,
                              int pitch, int x, int y)
{
    if (x < crop.x || y < crop.y || x >= crop.x + crop.width || y >= crop.y + crop.height)
        return crop.background;
    const auto *row = reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(pixels) + size_t(y - crop.y) * pitch);
    return row[x - crop.x];
}

inline void copyImageCropRow(const ImageCrop &crop, const void *pixels,
                             int pitch, int y, uint32_t *out)
{
    std::fill(out, out + crop.logicalWidth, crop.background);
    if (y >= crop.y && y < crop.y + crop.height)
        std::memcpy(out + crop.x,
                    static_cast<const uint8_t *>(pixels) + size_t(y - crop.y) * pitch,
                    size_t(crop.width) * 4);
}

#endif
