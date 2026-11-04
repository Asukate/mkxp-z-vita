#include "display/image-crop.h"
#include <cassert>
#include <cstdio>
#include <vector>

int main()
{
    const int w = 2480, h = 3508, pitch = w + 8;
    const uint32_t transparent = 0x00ffffff;
    std::vector<uint32_t> image(size_t(pitch) * h, transparent);
    for (int y = 35; y < 295; ++y)
        for (int x = 1307; x < 1531; ++x)
            image[size_t(y) * pitch + x] = 0x7f123456u + (x % 17);
    // Invisible RGB values are still part of Bitmap#get_pixel's contents.
    image[size_t(300) * pitch + 1250] = 0x00123456;
    const auto original = image;
    ImageCrop crop = findImageCrop(image.data(), w, h, pitch * 4);
    assert(crop.active() && crop.logicalWidth == w && crop.logicalHeight == h);
    assert(crop.x <= 1250 && crop.y <= 35);
    assert(size_t(crop.width) * crop.height * 4 < 400000);
    packImageCrop(image.data(), pitch * 4, crop);
    std::vector<uint32_t> row(w);
    for (int y = 0; y < h; ++y)
    {
        copyImageCropRow(crop, image.data(), crop.width * 4, y, row.data());
        for (int x = 0; x < w; ++x)
            assert(row[x] == original[size_t(y) * pitch + x]);
    }
    assert(imageCropPixel(crop, image.data(), crop.width * 4, 0, h - 1) == transparent);
    assert(imageCropPixel(crop, image.data(), crop.width * 4, 1250, 300) == 0x00123456);
    std::vector<uint32_t> solid(size_t(w) * h, 0xff556677);
    assert(!findImageCrop(solid.data(), w, h, w * 4).active());
    std::fill(solid.begin(), solid.end(), transparent);
    crop = findImageCrop(solid.data(), w, h, w * 4);
    assert(crop.active() && crop.width == 1 && crop.height == 1);
    solid.back() = 0x01000000;
    solid[w - 1] = solid[size_t(h - 1) * w] = 0x01000000;
    assert(!findImageCrop(solid.data(), w, h, w * 4).active());
    assert(!findImageCrop(solid.data(), 32, 32, 32 * 4).active());
    std::fill(solid.begin(), solid.end(), transparent);
    for (int y = 1; y < 2292; ++y)
        for (int x = 1; x < 1905; ++x)
            solid[size_t(y) * w + x] = 0x7f000000;
    crop = findImageCrop(solid.data(), w, h, w * 4);
    assert(crop.active()); // A half-sized light mask still saves roughly 16 MiB.
    puts("PASS: large sparse RGBA images retain every pixel, logical dimensions and hidden RGB; in-place packing and full-row restoration");
}
