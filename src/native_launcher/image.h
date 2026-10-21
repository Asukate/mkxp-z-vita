#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace hardrpg {
// A premultiplied RGBA bitmap for the launcher's CPU canvas. Pixels are
// stored row by row, four bytes each, alpha last.
struct Image {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;
  // Identifies these pixels for a GPU texture cache; touch() after changing them.
  uint64_t id = next();
  void touch() { id = next(); }
  static uint64_t next() {
    // Images are also decoded on the art loader's thread.
    static std::atomic<uint64_t> counter{0};
    return ++counter;
  }
  bool empty() const { return width <= 0 || height <= 0; }
  // Reads a PNG, JPEG or WebP file; returns an empty image when the file is
  // missing or broken.
  static Image load(const std::string &path);
  static Image decode(const std::vector<uint8_t> &bytes);
  // Writes the pixels as a JPEG (alpha is dropped: for opaque art only).
  bool saveJpeg(const std::string &path, int quality = 90) const;
  // A raw premultiplied copy with a small header, for cached art with alpha.
  bool saveRaw(const std::string &path) const;
  static Image loadRaw(const std::string &path);
  // A transparent w x h image.
  static Image blank(int w, int h);
  // A bilinear (or box, when shrinking a lot) resize to exactly w x h.
  Image resized(int w, int h) const;
  // A strong blur (three box passes), used to hide adult artwork.
  Image blurred(int radius) const;
  // Scales to cover w x h (cropping the overflow), as a CSS cover would.
  Image cover(int w, int h) const;
};
} // namespace hardrpg
