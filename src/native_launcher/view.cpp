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
  if (display)
    FT_Done_Face(display);
  if (face)
    FT_Done_Face(face);
  if (library)
    FT_Done_FreeType(library);
}
const View::Glyph &View::glyph(int size, FT_UInt index, bool alt) {
  FT_Face f = alt && display ? display : face;
  auto key = std::make_pair(f == face ? size : -size, index);
  auto found = glyphCache.find(key);
  if (found != glyphCache.end())
    return found->second;
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
  // Every screen but home is a sheet in the home screen's style.
  if (u.homeActive())
    buildHome(u);
  else
    buildSheet(u);
  if (!gpu)
    composite();
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
} // namespace hardrpg
