#include "view.h"
#include "release.h"
#include <algorithm>
#include <cmath>
#include <png.h>

namespace hardrpg {
constexpr int View::width;
constexpr int View::height;
namespace {
std::string tail(const std::string &s, size_t limit, size_t kept) {
  return s.size() > limit ? "..." + s.substr(s.size() - kept) : s;
}
uint32_t nextCharacter(const std::string &s, size_t &i) {
  uint8_t first = s[i++];
  if (first < 128)
    return first;
  int remaining = first >= 0xf0 && first <= 0xf4   ? 3
                  : first >= 0xe0 && first <= 0xef ? 2
                  : first >= 0xc2 && first <= 0xdf ? 1
                                                   : 0;
  uint32_t cp = first & ((1u << (6 - remaining)) - 1);
  if (!remaining)
    return 0xfffd;
  for (int n = 0; n < remaining; ++n) {
    if (i >= s.size() || (uint8_t(s[i]) & 0xc0) != 0x80)
      return 0xfffd;
    cp = (cp << 6) | (uint8_t(s[i++]) & 63);
  }
  return cp < (remaining == 1   ? 0x80u
               : remaining == 2 ? 0x800u
                                : 0x10000u) ||
                 cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)
             ? 0xfffd
             : cp;
}
const char *menuNames[] = {"Games", "Settings", "About", "Exit"};
} // namespace
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
}
View::~View() {
  if (face)
    FT_Done_Face(face);
  if (library)
    FT_Done_FreeType(library);
}
void View::pixel(int x, int y, Color color, unsigned alpha) {
  if (x < 0 || y < 0 || x >= width || y >= height)
    return;
  auto *p = &pixels[(y * width + x) * 4];
  for (int c = 0; c < 3; ++c)
    p[c] = (p[c] * (255 - alpha) + color[c] * alpha + 127) / 255;
  p[3] = 255;
}
void View::fill(int x, int y, int w, int h, Color c) {
  for (int row = std::max(0, y); row < std::min(height, y + h); ++row)
    for (int col = std::max(0, x); col < std::min(width, x + w); ++col)
      pixel(col, row, c);
}
void View::gradient(int x, int y, int w, int h, Color top, Color bottom) {
  for (int row = 0; row < h; ++row) {
    Color c;
    for (int i = 0; i < 3; ++i)
      c[i] = top[i] + std::lround((bottom[i] - top[i]) * row /
                                  double(std::max(1, h - 1)));
    fill(x, y + row, w, 1, c);
  }
}
void View::frame(int x, int y, int w, int h) {
  fill(x, y, w, h, {242, 226, 227});
  fill(x + 2, y + 2, w - 4, h - 4, {137, 21, 39});
  fill(x + 4, y + 4, w - 8, h - 8, {248, 238, 236});
  gradient(x + 6, y + 6, w - 12, h - 12, {108, 8, 25}, {44, 3, 13});
}
const View::Glyph &View::glyph(int size, FT_UInt index) {
  auto key = std::make_pair(size, index);
  auto found = glyphCache.find(key);
  if (found != glyphCache.end())
    return found->second;
  if (FT_Load_Glyph(face, index, FT_LOAD_RENDER))
    throw std::runtime_error("Cannot render launcher glyph");
  auto &b = face->glyph->bitmap;
  Glyph g{face->glyph->bitmap_left,
          face->glyph->bitmap_top,
          int(face->glyph->advance.x >> 6),
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
void View::text(int x, int y, int w, int h, const std::string &s, int size,
                Color color) {
  // Bound the cache when browsing many distinct Unicode names. Clear before
  // taking any glyph references, never in the middle of drawing a label.
  if (glyphBytes > 2 * 1024 * 1024 || glyphCache.size() > 2048) {
    glyphCache.clear();
    glyphBytes = 0;
  }
  FT_Set_Pixel_Sizes(face, 0, size);
  int asc = face->size->metrics.ascender >> 6,
      desc = face->size->metrics.descender >> 6;
  int baseline = y + (h - (asc - desc)) / 2 + asc;
  struct PositionedGlyph {
    int x, y;
    const Glyph *bitmap;
  };
  std::vector<PositionedGlyph> glyphs;
  int pen = x;
  FT_UInt last = 0;
  for (size_t i = 0; i < s.size();) {
    auto cp = nextCharacter(s, i);
    auto index = FT_Get_Char_Index(face, cp);
    if (!index)
      index = FT_Get_Char_Index(face, '?');
    if (last && index && FT_HAS_KERNING(face)) {
      FT_Vector delta;
      FT_Get_Kerning(face, last, index, FT_KERNING_DEFAULT, &delta);
      pen += delta.x >> 6;
    }
    last = index;
    const auto &g = glyph(size, index);
    glyphs.push_back({pen + g.left, baseline - g.top, &g});
    pen += g.advance;
  }
  int advance = std::max(1, pen - x);
  // Mapping each column once avoids a libm round call for every glyph pixel.
  std::vector<std::vector<int>> columns;
  for (auto &g : glyphs) {
    columns.emplace_back(g.bitmap->width);
    for (int col = 0; col < g.bitmap->width; ++col) {
      int offset = g.x + col - x;
      if (advance > w) {
        auto n = int64_t(offset) * w;
        offset = n < 0 ? -int((-2 * n + advance) / (2 * advance))
                       : int((2 * n + advance) / (2 * advance));
      }
      columns.back()[col] = x + offset;
    }
  }
  for (int pass = 0; pass < 2; ++pass)
    for (size_t i = 0; i < glyphs.size(); ++i) {
      const auto &g = glyphs[i];
      const auto &b = *g.bitmap;
      for (int row = 0; row < b.height; ++row)
        for (int col = 0; col < b.width; ++col) {
          auto alpha = b.mask[row * b.width + col];
          if (!alpha)
            continue;
          int px = columns[i][col] + (pass ? 0 : 1),
              py = g.y + row + (pass ? 0 : 1);
          if (px >= x && px < x + w && py >= y && py < y + h)
            pixel(px, py, pass ? color : Color{0, 0, 0}, alpha);
        }
    }
}
void View::highlight(int y, bool active, int x, int w) {
  fill(x, y, w, 35, active ? Color{255, 231, 231} : Color{173, 93, 105});
  gradient(x + 2, y + 2, w - 4, 31, {177, 28, 52}, {117, 12, 34});
}
void View::settings(const UI &u) {
  std::string hint;
  if (u.page == Page::Display) {
    text(194, 104, 716, 35, "Game display", 28);
    const char *keys[] = {"fixedAspectRatio", "integerScalingActive",
                          "smoothScaling"};
    const char *labels[] = {"Aspect ratio", "Scaling", "Filtering"};
    const char *names[][2] = {{"Fill screen", "Original"},
                              {"Fit screen", "Whole pixels"},
                              {"Nearest", "Bilinear"}};
    for (int i = 0; i < 3; ++i) {
      int y = 151 + i * 42;
      if (i == u.setting && u.focus == Focus::Settings)
        highlight(y, true);
      auto entry = u.displayValues.as_object().find(keys[i]);
      int value = i == 0 ? 1 : 0;
      bool custom = false;
      if (entry != u.displayValues.as_object().end()) {
        auto &v = entry->second;
        if (i == 2) {
          custom = !v.is_integer() || v.as_integer() < 0 || v.as_integer() > 1;
          if (!custom)
            value = v.as_integer();
        } else {
          custom = !v.is_boolean();
          if (!custom)
            value = v.as_boolean();
        }
      }
      text(202, y, 220, 35, labels[i], 24);
      fill(434, y + 8, 1, 19, {133, 65, 78});
      text(458, y, 440, 35, custom ? "Custom" : names[i][value], 23);
    }
    text(194, 306, 716, 25,
         "Applies on the next game launch. Per-game settings can override.",
         18);
    hint = "Cross/Left/Right: change   Square: default   Circle: Settings";
  } else if (u.page == Page::Folders) {
    text(194, 104, 716, 35, "Game folders", 28);
    text(194, 142, 716, 26, "Default: " + u.model.paths.at("games"), 18,
         {239, 188, 194});
    auto paths = u.gameFolders;
    paths.insert(paths.begin(), "Add another folder...");
    for (int row = 0; row < 7 && row + u.settingFirst < int(paths.size());
         ++row) {
      int y = 183 + row * 36;
      if (row + u.settingFirst == u.setting && u.focus == Focus::Settings)
        highlight(y, true);
      text(194, y, 716, 35, tail(paths[row + u.settingFirst], 72, 69), 21);
    }
    text(194, 443, 716, 25,
         "Extra folders are searched on launch and Triangle refresh.", 18);
    hint = "Cross: browse/add   Square: remove reference   Circle: Settings";
  } else if (u.page == Page::Rtp) {
    text(194, 104, 716, 35, "RTP locations", 28);
    const auto &paths = u.rtpChoices;
    for (int i = 0; i < 3; ++i) {
      int y = 151 + i * 86;
      if (i == u.rtpSelected && u.focus == Focus::Settings)
        highlight(y, true);
      text(194, y, 716, 35,
           packLabel(packs[i]) + " RTP - " +
               (i < int(u.rtpStatuses.size()) ? u.rtpStatuses[i] : ""),
           24);
      auto entry = paths.as_object().find(packs[i]);
      std::string path =
          entry == paths.as_object().end()
              ? u.model.paths.at(std::string("rtp/") + packs[i]) +
                    " (automatic)"
              : entry->second.as_string();
      text(194, y + 39, 716, 28, tail(path, 78, 75), 17, {239, 188, 194});
    }
    text(194, 443, 716, 25,
         "Packs stay where they are. RTP ZIPs are read directly.", 18);
    hint =
        "Cross: choose folder/ZIP   Square: automatic location   Circle: back";
  } else if (u.page == Page::Errors) {
    text(194, 104, 716, 35, "Game error history", 28);
    for (int row = 0; row < 7 && row + u.settingFirst < int(u.errors.size());
         ++row) {
      auto &record = u.errors[row + u.settingFirst];
      int y = 151 + row * 42;
      if (row + u.settingFirst == u.setting && u.focus == Focus::Settings)
        highlight(y, true);
      text(202, y, 405, 35, record.game, 21);
      text(614, y + 3, 284, 28, record.timestamp, 16, {239, 188, 194});
    }
    if (u.errors.empty())
      text(202, 151, 708, 35, "No game errors have been recorded.", 23);
    scrollbar(922, 151, 294, u.settingFirst, 7, u.errors.size());
    hint = "Cross: view trace   Square: export selected   Circle: Settings";
  } else {
    text(194, 104, 716, 35, "Preferences", 26);
    const char *names[] = {"Game display", "RTP locations", "Game folders",
                           "Game error history"};
    for (int i = 0; i < 4; ++i) {
      int y = 151 + i * 42;
      if (i == u.setting && u.focus == Focus::Settings)
        highlight(y, true);
      text(194, y, 716, 35, names[i], 25);
    }
    hint = "Up/Down: move   Cross: open   Circle: back";
  }
  fill(194, 140, 716, 1, {151, 70, 86});
  if (!u.message.empty())
    text(188, 474, 740, 24, u.message, 17, {255, 211, 172});
  text(188, 505, 736, 18, hint, 15);
}
void View::browser(const UI &u) {
  bool rtp = u.browseMode == BrowseMode::Rtp,
       pathPicker = rtp || u.browseMode == BrowseMode::Folder;
  text(28, 94, 121, 35,
       rtp          ? "Choose RTP"
       : pathPicker ? "Game folder"
                    : "Add games",
       20);
  text(26, 466, 124, 23, pathPicker ? "Square: folder" : "Square: scan", 14);
  text(26, 487, 124, 22, "Cross: folder", 14);
  text(26, 508, 124, 17, "Circle: back", 14);
  text(188, 93, 735, 26,
       tail(u.browser->path.empty() ? "Choose storage" : u.browser->path, 80,
            77),
       17, {238, 188, 191});
  for (int row = 0;
       row < 10 && row + u.browserFirst < int(u.browser->list.size()); ++row) {
    int y = 125 + row * 36;
    if (row + u.browserFirst == u.browserSelected)
      highlight(y, true);
    auto f = u.browser->list[row + u.browserFirst];
    text(194, y, 716, 35,
         f.name + (f.directory && !u.browser->path.empty() ? "/" : ""));
  }
  if (u.browser->list.empty())
    text(194, 155, 716, 38,
         pathPicker ? "Square selects this folder."
                    : "No subfolders. Square scans this folder.",
         22);
  auto message = u.message.empty() ? u.browser->error : u.message;
  if (!message.empty())
    text(188, 468, 740, 26, message, 17, {255, 211, 172});
  std::string hint =
      rtp ? "Cross: open folder/select ZIP   Square: use folder   Start: close"
      : pathPicker
          ? "Cross: open folder   Square: use folder   Start: close"
          : "Cross: open folder   Square: scan/add games   Start: close";
  text(188, 505, 736, 18, hint, 15);
}
void View::scrollbar(int x, int y, int h, int first, int rows, int count) {
  if (count <= rows)
    return;
  fill(x, y, 6, h, {89, 28, 44});
  int thumb = std::max(16, h * rows / count);
  int offset = (h - thumb) * first / std::max(1, count - rows);
  fill(x, y + offset, 6, thumb, {245, 189, 197});
}
void View::draw(const UI &u) {
  if (background.empty()) {
    gradient(0, 0, width, height, {63, 3, 15}, {23, 1, 9});
    frame(14, 14, 932, 58);
    frame(14, 82, 146, 448);
    frame(168, 82, 778, 448);
    background = pixels;
  } else {
    pixels = background;
  }
  text(26, 24, 908, 38, "HardRPG - RPG Maker XP/VX/VX Ace launcher", 26,
       {255, 244, 241});
  if (!u.errorReport.empty()) {
    text(31, 92, 117, 35, "Games");
    text(190, 98, 724, 38, "The game ended with an error", 27);
    for (int i = 0; i < 12 && i + u.errorScroll < int(u.errorReport.size());
         ++i)
      text(190, 147 + i * 27, 730, 27, u.errorReport[i + u.errorScroll], 18);
    if (!u.message.empty())
      text(190, 476, 730, 24, u.message, 17, {255, 211, 172});
    text(190, 505, 730, 18,
         "Up/Down: scroll   Square: export report   Cross/Circle: back", 15);
    return;
  }
  if (!u.missingRtp.empty()) {
    frame(194, 154, 720, 276);
    text(216, 177, 672, 40, "Missing " + packLabel(u.missingRtp) + " RTP", 29);
    text(216, 228, 672, 32, "Choose an existing RTP folder or ZIP in Settings.",
         21);
    text(216, 268, 672, 28, "Default location:", 19);
    text(216, 302, 672, 28, u.model.paths.at("rtp/" + u.missingRtp), 19);
    text(216, 365, 672, 30, "Cross: Settings     Circle: back to games", 21);
    return;
  }
  if (u.browser) {
    browser(u);
    return;
  }
  for (int i = 0; i < 4; ++i) {
    if (i == u.menu)
      highlight(92 + i * 38, u.focus == Focus::Menu, 22, 130);
    text(31, 92 + i * 38, 117, 35, menuNames[i]);
  }
  if (u.menu == 1)
    settings(u);
  else if (u.menu == 2) {
    text(194, 101, 716, 35, "HardRPG " HARDRPG_VERSION, 28);
    text(194, 142, 716, 26,
         "RPG Maker XP/VX/VX Ace on Vita. Created by Asukate.", 20);
    text(194, 171, 716, 26, "https://github.com/Asukate/mkxp-z-vita", 19,
         {239, 188, 194});
    text(194, 200, 716, 26, "Based on mkxp-z and its Ruby/RGSS runtime.", 17,
         {239, 188, 194});
    fill(194, 235, 716, 1, {151, 70, 86});
    for (int i = 0; i < 9 && i + u.aboutScroll < int(sizeof(releaseNotes) /
                                                     sizeof(*releaseNotes));
         ++i)
      text(194, 246 + i * 26, 716, 26, releaseNotes[i + u.aboutScroll], 18);
    scrollbar(922, 246, 234, u.aboutScroll, 9,
              sizeof(releaseNotes) / sizeof(*releaseNotes));
    text(194, 505, 716, 18,
         u.focus == Focus::About
             ? "Up/Down or touch: release notes   Circle: back"
             : "Cross/Right: scroll release notes",
         15);
  } else if (u.menu == 3) {
    text(194, 115, 716, 38, "Exit HardRPG?", 28);
    text(194, 166, 716, 32, "Cross: return to LiveArea    Circle: back", 22);
  } else {
    if (u.focus == Focus::Sources) {
      text(194, 104, 716, 35, "Game library locations", 26);
      std::vector<std::string> lines;
      for (auto &path : u.sources) {
        for (size_t offset = 0; offset < path.size(); offset += 72)
          lines.push_back(path.substr(offset, 72));
        lines.push_back("");
      }
      int first = std::min(u.sourceScroll, std::max(0, int(lines.size()) - 12));
      for (int i = 0; i < 12 && i + first < int(lines.size()); ++i)
        text(194, 150 + i * 26, 716, 26, lines[i + first], 18);
      text(194, 505, 716, 18, "Up/Down: scroll paths   Circle: back", 15);
      return;
    }
    auto locations =
        u.model.stack.size() == 1
            ? u.sources
            : std::vector<std::string>{u.model.current().path() +
                                       (u.model.current().archive
                                            ? "!/" + u.model.current().inner
                                            : "")};
    std::vector<std::string> pathLines;
    for (auto &path : locations)
      for (size_t offset = 0; offset < path.size(); offset += 88)
        pathLines.push_back(path.substr(offset, 88));
    for (int i = 0; i < 3 && i < int(pathLines.size()); ++i)
      text(194, 92 + i * 18, 716, 18, pathLines[i], 15, {238, 188, 191});
    if (pathLines.size() > 3)
      text(194, 128, 716, 18, "More locations: L+R or touch the paths", 15,
           {238, 188, 191});
    fill(188, 149, 738, 28, {88, 10, 29});
    text(194, 149, 380, 28,
         u.query.empty() ? "Select: search games" : "Search: " + u.query, 18);
    const char *engines[] = {"All engines", "XP", "VX", "VX Ace"};
    text(594, 149, 318, 28,
         std::string("L/R: ") + engines[u.engineFilter] +
             (u.engineFilter ? " [FILTER ACTIVE]" : ""),
         17, {255, 211, 172});
    auto list = u.visible();
    if (list.empty()) {
      text(194, 200, 716, 38,
           u.model.list.empty() ? "No games found in this folder."
                                : "No games match this search/filter.",
           23);
      text(194, 247, 716, 30, "Select: edit/clear search    L/R: change engine",
           19);
    } else {
      for (int row = 0; row < u.gameRows() && row + u.first < int(list.size());
           ++row) {
        auto &e = u.model.list[list[row + u.first]];
        int y = u.gameTop() + row * 32;
        if (row + u.first == u.selected && u.focus == Focus::Games)
          highlight(y, true, 184, 730);
        text(194, y, 650, 32, e.name, 23);
        auto tag = e.kind == Kind::Folder ? "DIR"
                   : e.version == 1       ? "XP"
                   : e.version == 2       ? "VX"
                   : e.version == 3       ? "ACE"
                                          : "?";
        text(848, y + 3, 65, 26, tag, 15, {242, 199, 199});
      }
      scrollbar(920, u.gameTop(), u.gameRows() * 32, u.first, u.gameRows(),
                list.size());
      text(795, 476, 118, 22,
           std::to_string(u.selected + 1) + "/" + std::to_string(list.size()),
           15);
    }
    auto message = u.message.empty() ? u.model.error : u.message;
    if (!message.empty())
      text(188, 476, 600, 22, message, 16, {255, 211, 172});
  }
  text(26, 467, 124, 22, "Start: add games", 14, {239, 188, 194});
  text(26, 487, 124, 22, "Cross: select", 15, {239, 188, 194});
  text(26, 508, 124, 17, "Circle: back", 14, {239, 188, 194});
  if (u.menu == 0)
    text(188, 505, 736, 18,
         u.focus == Focus::Games ? "Cross: play   Square: rename   Select: "
                                   "search   L/R: engine   Touch: scroll"
                                 : "Cross/Right: games   Select: search   L/R: "
                                   "engine   Triangle: refresh",
         14);
  if (!u.launchingName.empty()) {
    frame(240, 214, 640, 136);
    text(260, 228, 598, 44, u.launchingName, 29);
    text(260, 286, 598, 36, u.message.empty() ? "Starting game..." : u.message,
         19, {255, 211, 172});
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
} // namespace hardrpg
