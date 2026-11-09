#include "view.h"
#include <algorithm>
#include <png.h>

namespace hardrpg {
constexpr int View::width;
constexpr int View::height;
View::View(const std::string &font)
    : pixels(width * height * 4, 255), fontData(readFile(font, 4194304)) {
  if (FT_Init_FreeType(&library) ||
      FT_New_Memory_Face(library,
                         reinterpret_cast<const FT_Byte *>(fontData.data()),
                         fontData.size(), 0, &face)) {
    if (library)
      FT_Done_FreeType(library);
    library = nullptr;
    throw std::runtime_error("Cannot open launcher font");
  }
  surface = pixels.data();
  // Home screen assets live next to the font: app0:/ on the Vita, or the
  // launcher folder for the desktop preview.
  auto slash = font.find_last_of('/');
  std::string dir = slash == std::string::npos ? "" : font.substr(0, slash + 1);
  for (auto candidate : {dir, std::string("launcher/")}) {
    if (assets.empty() && !Image::load(candidate + "ui/gear-35.png").empty())
      assets = candidate;
  }
  try {
    displayData = readFile(assets + "font-display.ttf", 4194304);
    if (FT_New_Memory_Face(library, reinterpret_cast<const FT_Byte *>(displayData.data()),
                           displayData.size(), 0, &display))
      display = nullptr;
  } catch (const std::exception &) {
    display = nullptr;
  }
}
View::~View() {
  if (cjk)
    FT_Done_Face(cjk);
  if (display)
    FT_Done_Face(display);
  if (face)
    FT_Done_Face(face);
  if (library)
    FT_Done_FreeType(library);
}
FT_Face View::cjkFace() {
  if (!cjkTried) {
    cjkTried = true;
    for (auto path : {assets + "font-cjk.ttf", std::string("assets/wqymicrohei.ttf")}) {
      if (cjk)
        break;
      cjkData = readFile(path, 16u * 1024 * 1024);
      if (cjkData.empty() ||
          FT_New_Memory_Face(library, reinterpret_cast<const FT_Byte *>(cjkData.data()),
                             cjkData.size(), 0, &cjk)) {
        cjk = nullptr;
        cjkData.clear();
      }
    }
  }
  return cjk;
}
View::GlyphRef View::glyphFor(uint32_t c, bool alt) {
  int font = alt && display ? 1 : 0;
  FT_Face f = font ? display : face;
  if (FT_UInt index = FT_Get_Char_Index(f, c))
    return {font, index};
  if (c >= 0x80)
    if (FT_Face fallback = cjkFace())
      if (FT_UInt index = FT_Get_Char_Index(fallback, c))
        return {2, index};
  return {font, FT_Get_Char_Index(f, '?')};
}
const View::Glyph &View::glyph(int size, GlyphRef ref) {
  auto key = std::make_pair(size * 4 + ref.font, ref.index);
  auto found = glyphCache.find(key);
  if (found != glyphCache.end())
    return found->second;
  FT_Face f = ref.font == 2 ? cjk : ref.font == 1 ? display : face;
  FT_UInt index = ref.index;
  FT_Set_Pixel_Sizes(f, 0, size);
  if (FT_Load_Glyph(f, index, FT_LOAD_RENDER))
    throw std::runtime_error("Cannot render launcher glyph");
  auto &b = f->glyph->bitmap;
  Glyph g{f->glyph->bitmap_left,
          f->glyph->bitmap_top,
          int(f->glyph->advance.x >> 6),
          int(b.width),
          int(b.rows),
          {}};
  g.mask.resize(g.width * g.height);
  for (int r = 0; r < g.height; ++r)
    std::copy_n(b.buffer +
                    (b.pitch < 0 ? g.height - 1 - r : r) * std::abs(b.pitch),
                g.width, g.mask.data() + r * g.width);
  glyphBytes += g.mask.size();
  return glyphCache.emplace(key, std::move(g)).first->second;
}
void View::draw(const UI &u) {
  surface = pixels.data();
  surfaceW = width;
  surfaceH = height;
  marqueeMoving = false; // only the list's selected name moves
  // Every screen but home is a sheet in the home screen's style.
  if (u.welcome.active)
    buildWelcome(u);
  else if (u.homeActive())
    buildHome(u);
  else
    buildSheet(u);
  if (!gpu) {
    if (overlay)
      std::fill(pixels.begin(), pixels.end(), 0);
    composite();
  }
}
void View::splash(const std::string &path) {
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(&image, path.c_str()))
    throw std::runtime_error("Splash artwork could not be displayed.");
  if (image.width != width || image.height != height) {
    png_image_free(&image);
    throw std::runtime_error("Splash artwork dimensions differ.");
  }
  image.format = PNG_FORMAT_RGBA;
  if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr)) {
    png_image_free(&image);
    throw std::runtime_error("Splash artwork could not be displayed.");
  }
  png_image_free(&image);
}
void View::save(const std::string &path) const {
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = width;
  image.height = height;
  image.format = PNG_FORMAT_RGBA;
  if (!png_image_write_to_file(&image, path.c_str(), 0, pixels.data(), 0,
                               nullptr))
    throw std::runtime_error("Cannot write launcher preview");
}
const Image &View::statsPanel(const std::string &headline, int tone, const std::vector<StatRow> &rows, int width,
                              bool framed) {
  std::string key = headline + "|" + std::to_string(tone) + "|" + std::to_string(width) + (framed ? "|f" : "|t");
  for (auto &r : rows)
    key += "|" + r.name + "=" + r.value + "/" + std::to_string(r.tone);
  const int captionH = 13, lineH = 15, gap = 4, top = 32;
  struct Line { std::string text; bool caption; int tone; };
  std::vector<Line> lines;
  for (auto &r : rows) {
    if (!r.name.empty())
      lines.push_back({r.name, true, 0});
    size_t start = 0;
    for (;;) {
      size_t end = r.value.find('\n', start);
      lines.push_back({r.value.substr(start, end == std::string::npos ? std::string::npos : end - start), false, r.tone});
      if (end == std::string::npos)
        break;
      start = end + 1;
    }
  }
  int height = top + (lines.empty() ? 0 : 6);
  for (size_t i = 0; i < lines.size(); ++i)
    height += lines[i].caption ? captionH + (i ? gap : 0) : lineH;
  if (beginLayer(statsLayer, key, width, height)) {
    const Color tones[] = {{236, 236, 238}, {92, 214, 140}, {244, 190, 72}, {232, 68, 90}};
    if (framed) {
      rect(0, 0, width, height, {10, 10, 12}, 196, 10);
      rect(0, 0, width, height, {255, 255, 255}, 16, 10);
      rect(1, 1, width - 2, height - 2, {10, 10, 12}, 150, 9);
    }
    label(9, 5, headline, 22, tones[std::max(0, std::min(3, tone))], 255, true, -1, width - 18);
    int y = top;
    for (size_t i = 0; i < lines.size(); ++i) {
      const Line &l = lines[i];
      if (l.caption) {
        y += i ? gap : 0;
        label(9, y, l.text, 10, {150, 150, 158}, 255, false, -1, width - 18);
        y += captionH;
      } else {
        label(9, y, l.text, 13, tones[std::max(0, std::min(3, l.tone))], 255, false, -1, width - 18);
        y += lineH;
      }
    }
    endLayer();
  }
  return statsLayer.image;
}
} // namespace hardrpg
