#include "image.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>

namespace hardrpg {
namespace {
constexpr int maxSide = 4096; // launcher art is small; refuse anything larger
void premultiply(Image &img) {
  for (size_t i = 0; i < img.rgba.size(); i += 4) {
    unsigned a = img.rgba[i + 3];
    if (a == 255)
      continue;
    for (int c = 0; c < 3; ++c)
      img.rgba[i + c] = (img.rgba[i + c] * a + 127) / 255;
  }
}
bool readFile(const std::string &path, std::vector<uint8_t> &out) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  bool ok = size > 0 && size < 32 * 1024 * 1024;
  if (ok) {
    out.resize(size_t(size));
    ok = std::fread(out.data(), 1, out.size(), f) == out.size();
  }
  std::fclose(f);
  return ok;
}
Image decodePng(const std::vector<uint8_t> &bytes) {
  Image out;
  png_image png;
  std::memset(&png, 0, sizeof png);
  png.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&png, bytes.data(), bytes.size()))
    return out;
  if (png.width > maxSide || png.height > maxSide) {
    png_image_free(&png);
    return out;
  }
  png.format = PNG_FORMAT_RGBA;
  out.rgba.resize(PNG_IMAGE_SIZE(png));
  if (!png_image_finish_read(&png, nullptr, out.rgba.data(), 0, nullptr)) {
    png_image_free(&png);
    return Image{};
  }
  out.width = png.width;
  out.height = png.height;
  premultiply(out);
  return out;
}
Image decodeJpeg(const std::vector<uint8_t> &bytes) {
  Image out;
  tjhandle tj = tjInitDecompress();
  if (!tj)
    return out;
  int w = 0, h = 0, sub = 0, space = 0;
  auto *data = const_cast<unsigned char *>(bytes.data());
  if (tjDecompressHeader3(tj, data, bytes.size(), &w, &h, &sub, &space) == 0 && w > 0 && h > 0 && w <= maxSide &&
      h <= maxSide) {
    out.rgba.resize(size_t(w) * h * 4);
    if (tjDecompress2(tj, data, bytes.size(), out.rgba.data(), w, 0, h, TJPF_RGBA, TJFLAG_FASTDCT) == 0) {
      out.width = w;
      out.height = h;
    } else
      out.rgba.clear();
  }
  tjDestroy(tj);
  return out;
}
Image decodeWebp(const std::vector<uint8_t> &bytes) {
  Image out;
  int w = 0, h = 0;
  if (!WebPGetInfo(bytes.data(), bytes.size(), &w, &h) || w <= 0 || h <= 0 || w > maxSide || h > maxSide)
    return out;
  out.rgba.resize(size_t(w) * h * 4);
  if (!WebPDecodeRGBAInto(bytes.data(), bytes.size(), out.rgba.data(), out.rgba.size(), w * 4)) {
    out.rgba.clear();
    return out;
  }
  out.width = w;
  out.height = h;
  premultiply(out);
  return out;
}
} // namespace

Image Image::load(const std::string &path) {
  std::vector<uint8_t> bytes;
  if (!readFile(path, bytes))
    return Image{};
  return decode(bytes);
}
Image Image::decode(const std::vector<uint8_t> &b) {
  if (b.size() >= 8 && !std::memcmp(b.data(), "\x89PNG", 4))
    return decodePng(b);
  if (b.size() >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff)
    return decodeJpeg(b);
  if (b.size() >= 12 && !std::memcmp(b.data(), "RIFF", 4) && !std::memcmp(b.data() + 8, "WEBP", 4))
    return decodeWebp(b);
  return Image{};
}
bool Image::saveJpeg(const std::string &path, int quality) const {
  if (empty())
    return false;
  tjhandle tj = tjInitCompress();
  if (!tj)
    return false;
  unsigned char *jpeg = nullptr;
  unsigned long size = 0;
  bool ok = tjCompress2(tj, rgba.data(), width, 0, height, TJPF_RGBA, &jpeg, &size, TJSAMP_420, quality, TJFLAG_FASTDCT) == 0;
  if (ok) {
    // Write beside the target, then rename, so a half-written file is never read.
    auto temp = path + ".part";
    FILE *f = std::fopen(temp.c_str(), "wb");
    ok = f && std::fwrite(jpeg, 1, size, f) == size;
    if (f)
      ok = std::fclose(f) == 0 && ok;
    // The Vita will not rename over an existing file.
    if (ok)
      std::remove(path.c_str());
    ok = ok && std::rename(temp.c_str(), path.c_str()) == 0;
    if (!ok)
      std::remove(temp.c_str());
  }
  tjFree(jpeg);
  tjDestroy(tj);
  return ok;
}
bool Image::saveRaw(const std::string &path) const {
  if (empty())
    return false;
  auto temp = path + ".part";
  FILE *f = std::fopen(temp.c_str(), "wb");
  if (!f)
    return false;
  uint32_t header[3] = {0x31475248u /* "HRG1" */, uint32_t(width), uint32_t(height)};
  bool ok = std::fwrite(header, sizeof header, 1, f) == 1 && std::fwrite(rgba.data(), 1, rgba.size(), f) == rgba.size();
  ok = std::fclose(f) == 0 && ok;
  if (ok)
    std::remove(path.c_str());
  ok = ok && std::rename(temp.c_str(), path.c_str()) == 0;
  if (!ok)
    std::remove(temp.c_str());
  return ok;
}
Image Image::loadRaw(const std::string &path) {
  Image out;
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return out;
  uint32_t header[3] = {};
  if (std::fread(header, sizeof header, 1, f) == 1 && header[0] == 0x31475248u && header[1] && header[2] &&
      header[1] <= uint32_t(maxSide) && header[2] <= uint32_t(maxSide)) {
    out.rgba.resize(size_t(header[1]) * header[2] * 4);
    if (std::fread(out.rgba.data(), 1, out.rgba.size(), f) == out.rgba.size()) {
      out.width = int(header[1]);
      out.height = int(header[2]);
    } else
      out.rgba.clear();
  }
  std::fclose(f);
  return out;
}
Image Image::resized(int w, int h) const {
  Image out;
  if (empty() || w <= 0 || h <= 0)
    return out;
  out.width = w;
  out.height = h;
  out.rgba.resize(size_t(w) * h * 4);
  if (width >= 2 * w || height >= 2 * h) {
    // Box filter: average every source pixel that lands in each target pixel.
    for (int y = 0; y < h; ++y) {
      int y0 = y * height / h, y1 = std::max(y0 + 1, (y + 1) * height / h);
      for (int x = 0; x < w; ++x) {
        int x0 = x * width / w, x1 = std::max(x0 + 1, (x + 1) * width / w);
        unsigned sum[4] = {0, 0, 0, 0}, n = 0;
        for (int sy = y0; sy < y1; ++sy) {
          const uint8_t *p = &rgba[(size_t(sy) * width + x0) * 4];
          for (int sx = x0; sx < x1; ++sx, p += 4, ++n)
            for (int c = 0; c < 4; ++c)
              sum[c] += p[c];
        }
        uint8_t *d = &out.rgba[(size_t(y) * w + x) * 4];
        for (int c = 0; c < 4; ++c)
          d[c] = (sum[c] + n / 2) / n;
      }
    }
    return out;
  }
  for (int y = 0; y < h; ++y) {
    // 16.16 fixed point sample centres.
    int sy = std::max<int64_t>(0, (int64_t(2 * y + 1) * height * 32768) / h - 32768);
    int y0 = std::min(height - 1, sy >> 16), y1 = std::min(height - 1, y0 + 1);
    unsigned fy = (sy >> 8) & 255;
    for (int x = 0; x < w; ++x) {
      int sx = std::max<int64_t>(0, (int64_t(2 * x + 1) * width * 32768) / w - 32768);
      int x0 = std::min(width - 1, sx >> 16), x1 = std::min(width - 1, x0 + 1);
      unsigned fx = (sx >> 8) & 255;
      const uint8_t *a = &rgba[(size_t(y0) * width + x0) * 4], *b = &rgba[(size_t(y0) * width + x1) * 4],
                    *c = &rgba[(size_t(y1) * width + x0) * 4], *d = &rgba[(size_t(y1) * width + x1) * 4];
      uint8_t *o = &out.rgba[(size_t(y) * w + x) * 4];
      for (int k = 0; k < 4; ++k) {
        unsigned top = a[k] * (256 - fx) + b[k] * fx, bottom = c[k] * (256 - fx) + d[k] * fx;
        o[k] = (top * (256 - fy) + bottom * fy + 32768) >> 16;
      }
    }
  }
  return out;
}
Image Image::blank(int w, int h) {
  Image out;
  out.width = w;
  out.height = h;
  out.rgba.assign(size_t(w) * h * 4, 0);
  return out;
}
Image Image::blurred(int radius) const {
  Image out = *this;
  out.touch();
  if (empty() || radius < 1)
    return out;
  std::vector<uint8_t> line(size_t(std::max(width, height)) * 4);
  // One box pass along a row or column of n pixels, step apart.
  auto pass = [&](uint8_t *p, int n, size_t step) {
    for (int i = 0; i < n; ++i)
      std::memcpy(&line[size_t(i) * 4], p + i * step, 4);
    for (int c = 0; c < 4; ++c) {
      int sum = 0, span = 2 * radius + 1;
      for (int i = -radius; i <= radius; ++i)
        sum += line[size_t(std::min(n - 1, std::max(0, i))) * 4 + c];
      for (int i = 0; i < n; ++i) {
        p[i * step + c] = uint8_t(sum / span);
        sum += line[size_t(std::min(n - 1, i + radius + 1)) * 4 + c] - line[size_t(std::max(0, i - radius)) * 4 + c];
      }
    }
  };
  for (int round = 0; round < 3; ++round) {
    for (int y = 0; y < height; ++y)
      pass(&out.rgba[size_t(y) * width * 4], width, 4);
    for (int x = 0; x < width; ++x)
      pass(&out.rgba[size_t(x) * 4], height, size_t(width) * 4);
  }
  return out;
}
Image Image::cover(int w, int h) const {
  if (empty())
    return Image{};
  if (width == w && height == h) {
    Image same = *this;
    same.touch();
    return same;
  }
  // Scale so both sides cover the target, then crop the centre.
  int sw, sh;
  if (int64_t(width) * h > int64_t(height) * w) {
    sh = h;
    sw = std::max(w, int(int64_t(width) * h / height));
  } else {
    sw = w;
    sh = std::max(h, int(int64_t(height) * w / width));
  }
  Image scaled = resized(sw, sh), out;
  out.width = w;
  out.height = h;
  out.rgba.resize(size_t(w) * h * 4);
  int ox = (sw - w) / 2, oy = (sh - h) / 2;
  for (int y = 0; y < h; ++y)
    std::memcpy(&out.rgba[size_t(y) * w * 4], &scaled.rgba[(size_t(y + oy) * sw + ox) * 4], size_t(w) * 4);
  return out;
}
} // namespace hardrpg
