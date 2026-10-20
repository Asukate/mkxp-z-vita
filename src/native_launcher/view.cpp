#include "view.h"
#include "release.h"
#include <cctype>
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
  if (u.page == Page::Game) {
    text(194,104,716,35,"Game settings: " + u.gameEntry.name,25);
    const char *names[] = {"Controls",
                           "Display",
                           "Audio / MIDI",
                           "Performance and speed",
                           "RTP locations (shared)",
                           "Rename",
                           "Game information",
                           "Play",
                           "Remove from library",
                           "Delete the ZIP, keep the game"};
    for (int row = 0; row < 7 && row + u.settingFirst < u.gameOptionsCount();
         ++row) {
      int y = 151 + row * 42;
      if (row + u.settingFirst == u.setting)
        highlight(y, true);
      text(202, y, 696, 35, names[row + u.settingFirst], 24);
    }
    scrollbar(922, 151, 294, u.settingFirst, 7, u.gameOptionsCount());
    hint = "Up/Down: move   Cross: open   Circle: back to this game";
  } else if (u.page == Page::Audio) {
    text(194, 104, 716, 35, "Audio: " + u.gameEntry.name, 25);
    const char *keys[] = {"enableAudio", "midiReverb", "midiChorus"};
    const char *names[] = {"Audio", "MIDI reverb", "MIDI chorus"};
    for (int i = 0; i < 3; ++i) {
      int y = 151 + i * 42;
      if (i == u.setting)
        highlight(y, true);
      bool enabled = i == 0;
      auto value = u.displayValues.as_object().find(keys[i]);
      if (value != u.displayValues.as_object().end() &&
          value->second.is_boolean())
        enabled = value->second.as_boolean();
      text(202, y, 220, 35, names[i], 23);
      text(458, y, 440, 35, enabled ? "On" : "Off", 23);
    }
    text(194, 310, 716, 28, "Saved for this game. Applies on the next launch.",
         19);
    text(194, 352, 716, 28, "MIDI effects add CPU work; Off is the default.",
         18);
    hint = "Cross/Left/Right: change   Square: inherit   Circle: game settings";
  } else if (u.page == Page::Remove) {
    text(194, 104, 716, 35, "Remove: " + u.gameEntry.name, 25);
    auto q = u.removeQuestions[u.removeStep];
    bool source = !u.removeInfo.folder.empty() || !u.removeInfo.zip.empty();
    // A game whose ZIP is gone is only its unpacked copy: those are its files.
    bool copy = q == UI::DeleteUnpacked || (!source && u.gameEntry.node.archive);
    bool zipOnly = q == UI::DeleteZipOnly;
    std::string question = zipOnly ? "Delete the ZIP to free its space?"
                           : copy  ? "Delete its unpacked copy too?"
                                   : "Delete its game files too?";
    text(202, 151, 696, 30, zipOnly ? "The game stays in the list and plays from its unpacked copy."
                            : u.removeStep == 0 ? "It will be removed from the list."
                            : "Game files: " + std::string(u.removeFiles ? "will be deleted." : "kept."), 20);
    text(202, 186, 696, 34, question, 25);
    text(202, 226, 696, 24, tail(u.removePath(q), 74, 71), 16, {214, 190, 190});
    bool one = copy || zipOnly;
    const char *rows[] = {one ? "No, keep it" : "No, keep them", one ? "Yes, delete it" : "Yes, delete them"};
    for (int row = 0; row < 2; ++row) {
      int y = 280 + row * 42;
      if (row == u.setting)
        highlight(y, true);
      text(202, y, 696, 35, rows[row], 23);
    }
    if (u.message.empty())
      text(194, 447, 716, 30, zipOnly ? "Saves and settings stay: they are in the unpacked copy."
                              : copy ? "The unpacked copy holds the game's saves. Deleting cannot be undone."
                                     : "Saves inside the game files go with them. Deleting cannot be undone.", 16);
    hint = "Up/Down: choose   Cross: answer   Circle: cancel";
  } else if (u.page == Page::GameInfo) {
    text(194, 104, 716, 35, "Information: " + u.gameEntry.name, 25);
    const char *engines[] = {"Unknown", "RPG Maker XP", "RPG Maker VX",
                             "RPG Maker VX Ace"};
    text(202, 158, 696, 30, u.gameEntry.version == 4 ? "RPG Maker 2000 / 2003" : engines[u.gameEntry.version], 23);
    text(202, 201, 696, 27,
         u.gameEntry.node.archive ? "Source: ZIP archive"
                                  : "Source: game folder",
         21);
    text(202, 245, 696, 26, "Location:", 19);
    auto location = u.gameEntry.node.path();
    if (u.gameEntry.node.archive && !u.gameEntry.node.inner.empty())
      location += "!/" + u.gameEntry.node.inner;
    for (size_t i = 0; i < 3 && i * 72 < location.size(); ++i)
      text(202, 278 + i * 25, 696, 25, location.substr(i * 72, 72), 17);
    auto bank = u.displayValues.as_object().find("midiSoundFont");
    std::string soundfont = "Bundled General MIDI bank";
    if (bank != u.displayValues.as_object().end() && bank->second.is_string() &&
        !bank->second.as_string().empty())
      soundfont = bank->second.as_string();
    if (u.gameEntry.node.archive) {
      auto cache = u.model.paths.at("cache/zip-" + key(u.gameEntry.node.identity(u.model.paths)));
      text(202, 375, 696, 25, std::string(directory(cache) ? "Prepared copy: " : "Preparation folder: ") + tail(cache, 70, 67), 17);
      text(202, 409, 696, 26, "The unpacked copy is reused each time you play.", 18);
      text(202, 442, 696, 24, "Once unpacked, the ZIP can be deleted; the game stays listed.", 17);
    } else text(202, 383, 696, 28, "MIDI bank: " + tail(soundfont, 65, 62), 18);
    hint = "Circle: game settings";
  } else if (u.page == Page::Updates) {
    text(194, 104, 716, 35, "Release checks", 26);
    const std::string labels[] = {std::string("Check on startup: ") +
                                      (u.automaticUpdates ? "On" : "Off"),
                                  "Check now", "View newer release notes"};
    for (int i = 0; i < 3; ++i) {
      int y = 151 + i * 42;
      if (i == u.setting)
        highlight(y, true);
      text(202, y, 700, 35, labels[i], 22);
    }
    text(194, 310, 716, 28, "Current or offline: no startup notification.", 19);
    text(194, 346, 716, 28,
         "Checks published Alpha releases for an Alpha build.", 18);
    text(194, 382, 716, 28, "Updates are downloaded and installed manually.",
         18);
    hint = "Cross: select   Circle: Settings";
  } else if (u.page == Page::Display) {
    text(194, 104, 716, 35,
         u.gameOptions ? "Display: " + u.gameEntry.name
                       : "Default game display",
         25);
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
         u.gameOptions ? "Saved for this game. Square restores the global default." : "Default for games without a per-game override.",
         18);
    hint = u.gameOptions ? "Cross/Left/Right: change   Square: inherit   Circle: game settings" : "Cross/Left/Right: change   Square: default   Circle: Settings";
  } else if (u.page == Page::Metrics) {
    text(194, 104, 716, 35, u.gameOptions ? "Performance: " + u.gameEntry.name : "Performance and speed", 25);
    if (u.focus == Focus::Settings) highlight(151 + u.setting * 42, true);
    int mode = 0;
    auto i = u.displayValues.as_object().find("performanceMetrics");
    if (i != u.displayValues.as_object().end() && i->second.is_integer() &&
        i->second.as_integer() >= 0 && i->second.as_integer() <= 2)
      mode = i->second.as_integer();
    const char *modes[] = {"Off", "FPS", "FPS + frame timing"};
    text(202, 151, 220, 35, "Overlay", 24);
    fill(434, 159, 1, 19, {133, 65, 78});
    text(458, 151, 440, 35, modes[mode], 23);
    auto ff = u.displayValues.as_object().find("fastForward");
    bool fast = ff == u.displayValues.as_object().end() || !ff->second.is_boolean() ||
                ff->second.as_boolean();
    text(202, 193, 220, 35, "Fast forward", 24);
    fill(434, 201, 1, 19, {133, 65, 78});
    text(458, 193, 440, 35, fast ? "On (L+R)" : "Off", 23);
    if (u.setting == 1) {
      text(202, 253, 696, 28, "L+R switches up to 2x speed; >> shows while on.", 21);
      text(202, 295, 696, 28, "Busy maps run as fast as they can, below 2x.", 21);
      text(202, 337, 696, 28, "Any button can also be mapped to Fast forward in Controls.", 18);
    } else {
      text(202, 253, 696, 28, "FPS: frames presented each second.", 21);
      text(202, 295, 696, 28, "MS: average time between frames, including waits.", 21);
      text(202, 337, 696, 28, "Peak: longest frame in roughly the last 5 seconds (ms).", 21);
      text(202, 371, 696, 28, "Uses an empty border when it fits; otherwise a corner panel.", 18);
    }
    text(202, 405, 696, 28, "Applies on the next game launch.", 20);
    hint = u.gameOptions ? "Cross/Left/Right: change   Square: inherit   Circle: game settings" : "Cross/Left/Right: change   Square: default   Circle: Settings";
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
         "Folders are scanned automatically on each app boot.", 18);
    hint = "Cross: browse/add   Square: remove reference   Circle: Settings";
  } else if (u.page == Page::Rtp) {
    text(194, 104, 716, 35, "RTP locations (shared by all games)", 25);
    const auto &paths = u.rtpChoices;
    for (int i = 0; i < rtpPackCount; ++i) {
      int y = 151 + i * 54;
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
      text(194, y + 29, 716, 28, tail(path, 78, 75), 17, {239, 188, 194});
    }
    text(194, 443, 716, 25,
         "ZIP RTPs are prepared once. Original packs are kept.", 18);
    hint =
        "Cross: choose   Square: automatic   Triangle: refresh   Circle: back";
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
                           "Game error history", "Performance and speed", "Release checks"};
    for (int i = 0; i < 6; ++i) {
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
  text(26, 24, 908, 38, "HardRPG - RPG Maker game library", 26,
       {255, 244, 241});
  if(!u.releaseReport.empty()) {
    text(31,92,117,35,"Update",22);
    text(194,104,716,35,"New HardRPG release: "+u.releaseNotice.tag,26);
    for(int i=0;i<11 && i+u.releaseScroll<int(u.releaseReport.size());++i)
      text(194,151+i*28,716,28,u.releaseReport[i+u.releaseScroll],18);
    scrollbar(922,151,308,u.releaseScroll,11,u.releaseReport.size());
    text(194,505,716,20,"Up/Down: changelog   Cross/Circle: continue using HardRPG",15);
    return;
  }
  if(u.page==Page::Controls) {
    text(31,92,117,35,"Controls",22);
    text(194,104,716,35,u.controlPicking?u.controlLabel(u.setting)+" does: "+u.controlValue(u.setting)
         :u.controlsPad==2?"Rear touch pad: "+u.controlsEntry.name
         :u.controlsPad==1?"Front touch screen: "+u.controlsEntry.name
         :u.controlsAdvanced?"Sticks and pointer: "+u.controlsEntry.name:"Controls: "+u.controlsEntry.name,25);
    if(u.controlsTesting) {
      text(194,145,716,28,u.controls.enabled?"Profile preview (Circle exits)":"Raw inputs; enable profile to preview actions",18);
      for(int i=0;i<12;++i) {
        int x=194+(i/6)*365,y=184+(i%6)*23;
        text(x,y,350,23,std::string(u.controlsSample.buttons&(1u<<i)?"[ON] ":"[  ] ")+controlSources[i]+" -> "+controlName(u.controls.buttons[i]),17);
      }
      auto panel=[&](int x,const char *name,const std::vector<ControlContact> &contacts) {
        frame(x,338,340,100);text(x+5,340,300,20,name,16);
        for(auto &c:contacts)fill(x+int(std::max(0.f,std::min(1.f,c.x))*326),360+int(std::max(0.f,std::min(1.f,c.y))*64),10,10,{255,224,180});
      };
      panel(194,"Front touch",u.controlsSample.front);panel(562,"Rear touch",u.controlsSample.rear);
      text(194,442,716,24,"Sticks L: "+std::to_string(u.controlsSample.lx)+","+std::to_string(u.controlsSample.ly)+"  R: "+std::to_string(u.controlsSample.rx)+","+std::to_string(u.controlsSample.ry),17);
      std::string actions="Actions: ";for(auto &a:controlActions)if(a.code && u.controlsResult.actions[a.code])actions+=std::string(a.name)+" ";
      for(auto &k:controlPicks)if(controlIsKey(k.value) && u.controlsResult.keys[k.value-controlKeyBase])actions+=std::string("Key ")+k.name+" ";
      text(194,466,716,24,actions,16);
      text(194,502,716,20,"Touch zones press while held. Rear: fingers resting at launch are ignored.",15);
    } else if(u.controlPicking) {
      // What another input already holds is tinted, so gaps and clashes show.
      std::vector<int> taken={u.controls.frontLeft,u.controls.frontRight,u.controls.frontBottomLeft,u.controls.frontBottomRight,
                             u.controls.rearLeft,u.controls.rearRight,u.controls.rearBottomLeft,u.controls.rearBottomRight};
      taken.insert(taken.end(),u.controls.buttons.begin(),u.controls.buttons.end());
      for(int i=0;i<controlPickCount;++i) {
        int x,y,w,h;controlPickRect(i,x,y,w,h);
        bool here=i==u.controlPickKey,key=controlIsKey(controlPicks[i].value);
        bool used=controlPicks[i].value && std::find(taken.begin(),taken.end(),controlPicks[i].value)!=taken.end();
        fill(x,y,w,h,here?Color{214,74,60}:used?Color{104,66,60}:key?Color{52,44,46}:Color{74,40,52});
        std::string name=controlPicks[i].name;
        text(x+3,y+3,w-6,h-6,name,name.size()>5?12:name.size()>2?14:17);
      }
      text(194,463,716,30,"Top: RPG Maker actions (X=A: RPG X, the A key on PC). Below: keys. Tinted: in use.",16);
      text(194,505,716,20,"D-pad: move   Cross: assign   Circle: cancel   Touch: tap twice",15);
    } else if(u.controlsPad) {
      // The mode, then the surface as the player holds it, split into the
      // zones that mode reads; each zone takes a mapping. Nothing of this
      // is drawn in game.
      bool rear=u.controlsPad==2;
      int x,y,w,h;
      u.padRowRect(0,x,y,w,h);
      if(u.setting==0)highlight(y,true);
      text(202,y,355,35,"Mode",21);text(563,y,345,35,u.controlValue(0),19);
      fill(214,200,668,240,{28,22,26});fill(220,206,656,228,{58,44,50});
      text(228,206,640,16,rear?"Back of the console, left and right as you hold it":"The screen",12,{214,190,190});
      if(u.controlsCount()==1) {
        int mode=rear?u.controls.rear:u.controls.front;
        text(240,290,616,30,mode==2?(rear?"Trackpad: hold L and slide to move the pointer.":"Mouse pointer: touches move and click the pointer.")
             :"Off. Choose a mode with Left/Right to map zones.",17,{214,190,190});
      }
      for(int row=1;row<u.controlsCount();++row) {
        u.padRowRect(row,x,y,w,h);
        bool here=u.setting==row;
        fill(x,y,w,h,here?Color{214,74,60}:Color{120,52,60});
        std::string label=u.controlLabel(row);
        label=label.substr(label.find(' ')+1);label[0]=char(std::toupper(label[0]));
        text(x+10,y+8,w-20,22,label,15,{255,224,214});
        text(x+10,y+(h>120?h/2-20:38),w-20,40,u.controlValue(row),25,{255,241,237});
      }
      int mode=rear?u.controls.rear:u.controls.front;
      text(194,447,716,30,!u.message.empty()?u.message
           :rear?(mode==1?"Two zones along the top edge, clear of your grip.":"Fingers resting on the pad when the game starts are ignored.")
           :mode==1||mode==3?"These buttons are drawn on screen. Whole screen, halves and corners are invisible."
           :mode>=4?"Invisible zones: nothing is drawn on screen. Several fingers press several zones."
           :"Whole screen, halves and corners are invisible touch zones.",16);
      text(194,505,716,20,"Left/Right: mode or zone   Cross: map zone   Circle: back   Touch: tap twice",15);
    } else {
      for(int row=0;row<7 && row+u.settingFirst<u.controlsCount();++row) {
        int i=row+u.settingFirst,y=151+row*42;if(i==u.setting)highlight(y,true);
        text(202,y,355,35,u.controlLabel(i),21);text(563,y,345,35,u.controlValue(i),19);
      }
      scrollbar(922,151,294,u.settingFirst,7,u.controlsCount());
      text(194,463,716,30,!u.message.empty()?u.message:u.controlsAdvanced?"Pointer options need a game that reads the mouse.":u.setting>=1 && u.setting<=12?"Press Cross to give this button a keyboard key or an RPG Maker action.":u.controls.enabled?"Each button does what is shown. Saved per game.":"Pick a button below and press Cross to map it to any keyboard key.",16);
      text(194,505,716,20,u.controlsAdvanced?"Cross or Left/Right: change   Circle: back to buttons":"Cross: map button / change   Up/Down: select   Circle: game settings",15);
    }
    return;
  }
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
  if (u.gameOptions) {
    text(31,92,117,35,"Game",22);
    text(31,130,117,35,"settings",22);
    settings(u);
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
    text(194, 142, 716, 26, "RPG Maker games on Vita. Created by Asukate.", 20);
    text(194, 171, 716, 26, "https://github.com/Asukate/mkxp-z-vita", 19,
         {239, 188, 194});
    text(194, 200, 716, 26, "Powered by mkxp-z and EasyRPG Player.", 17,
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
    const char *engines[] = {"All engines", "XP", "VX", "VX Ace", "2000/2003"};
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
                   : engineFor(e.version) ? engineFor(e.version)->badge
                                          : "";
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
         u.focus == Focus::Games ? "Cross: play/open   Triangle: game settings   Select: "
                                   "search   L/R: engine   Touch: scroll"
                                 : "Cross/Right: games   Select: search   L/R: "
                                   "engine",
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
