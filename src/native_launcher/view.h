#pragma once
#include "ui.h"
#include <array>
#include <ft2build.h>
#include <map>
#include <vector>
#include FT_FREETYPE_H

namespace hardrpg {
class View {
public:
  static constexpr int width = 960, height = 544;
  explicit View(const std::string &font);
  ~View();
  View(const View &) = delete;
  std::vector<uint8_t> pixels;
  void draw(const UI &);
  void splash(const std::string &png);
  void save(const std::string &png) const;

private:
  std::vector<uint8_t> background;
  std::string fontData;
  FT_Library library = nullptr;
  FT_Face face = nullptr;
  struct Glyph {
    int left, top, advance, width, height;
    std::vector<uint8_t> mask;
  };
  std::map<std::pair<int, FT_UInt>, Glyph> glyphCache;
  size_t glyphBytes = 0;
  const Glyph &glyph(int size, FT_UInt index);
  using Color = std::array<int, 3>;
  void pixel(int x, int y, Color color, unsigned alpha = 255);
  void fill(int x, int y, int w, int h, Color);
  void gradient(int x, int y, int w, int h, Color top, Color bottom);
  void frame(int x, int y, int w, int h);
  void text(int x, int y, int w, int h, const std::string &, int size = 23,
            Color color = {255, 241, 237});
  void highlight(int y, bool active, int x = 184, int w = 744);
  void settings(const UI &);
  void browser(const UI &);
  void scrollbar(int x, int y, int h, int first, int rows, int count);
};
} // namespace hardrpg
