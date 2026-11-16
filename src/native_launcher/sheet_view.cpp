// Every screen other than home (settings, game settings, controls, the
// folder browser, About, reports and questions) as a "sheet": the home
// screen's top bar and hint bar around a header, a list of rows and notes.
//
// Rows are cached text layers; the focus is a GPU quad behind them, so moving
// through a list redraws no text. Row positions come from home_layout.h,
// which touch (UI::sheetTouch) uses too.
#include "home_layout.h"
#include "i18n.h"
#include "release.h"
#include "util/json5pp.hpp"
#include "view.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace hardrpg {
using namespace homeLayout;
namespace {
using Color = std::array<int, 3>;
const Color white{255, 255, 255}, ink{7, 7, 7}, muted{169, 169, 173}, faint{111, 111, 117}, ruby{224, 35, 63},
    rubyHi{255, 77, 99}, soft{214, 214, 216};
std::string tail(const std::string &s, size_t limit) {
  return s.size() > limit ? "..." + s.substr(s.size() - (limit - 3)) : s;
}
const char *engineLong(int version) {
  static const char *names[] = {"Unknown engine", "RPG Maker XP", "RPG Maker VX", "RPG Maker VX Ace", "RPG Maker 2000 / 2003",
                                "RPG Maker MV", "RPG Maker MZ", "RPG Maker Dante 98"};
  return tr(version >= 0 && version <= formatLast ? names[version] : names[0]);
}
// Upper case, including the accented letters of Latin-1 (á -> Á, ç -> Ç).
std::string upper(std::string s) {
  for (size_t i = 0; i < s.size(); ++i) {
    auto c = static_cast<unsigned char>(s[i]);
    if (c == 0xc3 && i + 1 < s.size()) {
      auto next = static_cast<unsigned char>(s[i + 1]);
      if (next >= 0xa0 && next <= 0xbe && next != 0xb7)
        s[i + 1] = char(next - 0x20);
      ++i;
    } else if (c < 0x80)
      s[i] = char(std::toupper(c));
  }
  return s;
}
} // namespace

std::vector<std::string> View::wrap(const std::string &text, int size, int maxWidth, bool alt) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    auto end = text.find('\n', start);
    auto para = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::string line, word;
    bool space = false, wordSpace = false;
    // Adds a word, or one CJK character, to the line; sticky stays on it even
    // when it overflows (closing punctuation never starts a line).
    auto place = [&](const std::string &piece, bool spaced, bool sticky) {
      std::string joined = line.empty() ? piece : line + (spaced ? " " : "") + piece;
      if (line.empty() || sticky || measure(joined, size, alt) <= maxWidth)
        line = joined;
      else {
        lines.push_back(line);
        line = piece;
      }
    };
    auto flush = [&] {
      if (!word.empty())
        place(word, wordSpace, false);
      word.clear();
    };
    for (size_t i = 0; i < para.size();) {
      unsigned char b = para[i];
      size_t n = b < 0x80 ? 1 : b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : 2;
      std::string ch = para.substr(i, n);
      uint32_t c = b < 0x80 ? b : b & (0x3f >> (n - 1));
      for (size_t k = 1; k < n && i + k < para.size(); ++k)
        c = (c << 6) | (static_cast<unsigned char>(para[i + k]) & 63);
      i += n;
      if (c == ' ') {
        flush();
        space = true;
      } else if ((c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0xff00 && c <= 0xffef)) {
        flush();
        static const char *closing[] = {"、", "。", "，", "．", "！", "？", "：", "；", "）", "」", "』", "】", "〉", "》", "ー", "…", "ぁ", "ぃ", "ぅ", "ぇ", "ぉ", "っ", "ゃ", "ゅ", "ょ", "ァ", "ィ", "ゥ", "ェ", "ォ", "ッ", "ャ", "ュ", "ョ"};
        bool sticky = false;
        for (auto p : closing)
          sticky = sticky || ch == p;
        place(ch, space, sticky);
        space = false;
      } else {
        if (word.empty()) {
          wordSpace = space;
          space = false;
        }
        word += ch;
      }
    }
    flush();
    lines.push_back(line);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return lines;
}

const Image &View::shoulder(int side, int &x, int &y) {
  Image &glow = shoulderGlow[side];
  if (glow.empty()) {
    const Image &art = icon("vita-640");
    if (art.empty() || art.width < 640)
      return glow;
    // The light grey wedge in the top corner; an opening (erode then
    // dilate by 2 px) drops the thin outline and the top rim it touches.
    const int x0 = side ? 420 : 0, x1 = side ? art.width : 220, y1 = std::min(40, art.height), r = 2;
    const int w = x1 - x0, h = y1;
    auto bright = [&](int px, int py) {
      if (px < x0 || px >= x1 || py < 0 || py >= y1)
        return false;
      const uint8_t *p = &art.rgba[(size_t(py) * art.width + px) * 4];
      return p[3] > 200 && (p[0] + p[1] + p[2]) > 140 * 3;
    };
    std::vector<uint8_t> core(size_t(w) * h), keep(size_t(w) * h);
    for (int py = 0; py < h; ++py)
      for (int px = 0; px < w; ++px) {
        bool all = true;
        for (int dy = -r; dy <= r && all; ++dy)
          for (int dx = -r; dx <= r && all; ++dx)
            all = bright(x0 + px + dx, py + dy);
        core[size_t(py) * w + px] = all;
      }
    int left = w, top = h, right = -1, bottom = -1;
    for (int py = 0; py < h; ++py)
      for (int px = 0; px < w; ++px) {
        if (!bright(x0 + px, py))
          continue;
        bool near = false;
        for (int dy = -r; dy <= r && !near; ++dy)
          for (int dx = -r; dx <= r && !near; ++dx) {
            int cx = px + dx, cy = py + dy;
            near = cx >= 0 && cy >= 0 && cx < w && cy < h && core[size_t(cy) * w + cx];
          }
        if (near) {
          keep[size_t(py) * w + px] = 1;
          left = std::min(left, px), right = std::max(right, px);
          top = std::min(top, py), bottom = std::max(bottom, py);
        }
      }
    if (right < 0)
      return glow;
    // One pixel of soft edge around the shape.
    left = std::max(0, left - 1), top = std::max(0, top - 1);
    right = std::min(w - 1, right + 1), bottom = std::min(h - 1, bottom + 1);
    glow = Image::blank(right - left + 1, bottom - top + 1);
    for (int py = top; py <= bottom; ++py)
      for (int px = left; px <= right; ++px) {
        int a = 0;
        if (keep[size_t(py) * w + px])
          a = 255;
        else
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              int cx = px + dx, cy = py + dy;
              if (cx >= 0 && cy >= 0 && cx < w && cy < h && keep[size_t(cy) * w + cx])
                a = 110;
            }
        uint8_t *q = &glow.rgba[(size_t(py - top) * glow.width + (px - left)) * 4];
        q[0] = uint8_t(ruby[0] * a / 255), q[1] = uint8_t(ruby[1] * a / 255), q[2] = uint8_t(ruby[2] * a / 255);
        q[3] = uint8_t(a);
      }
    glow.touch();
    shoulderX[side] = x0 + left;
    shoulderY[side] = top;
  }
  x = shoulderX[side];
  y = shoulderY[side];
  return glow;
}

namespace {
// 1234567 -> "1,234,567", as the cheat rows show numbers.
std::string groupDigits(long long n) {
  std::string d = std::to_string(n < 0 ? -n : n), out;
  for (size_t i = 0; i < d.size(); ++i) {
    if (i > 0 && (d.size() - i) % 3 == 0)
      out += ',';
    out += d[i];
  }
  return (n < 0 ? "-" : "") + out;
}
// Each "{}" in text replaced by the next argument.
std::string fillArgs(std::string text, const std::vector<std::string> &args) {
  size_t at = 0;
  for (auto &a : args) {
    at = text.find("{}", at);
    if (at == std::string::npos)
      break;
    text.replace(at, 2, a);
    at += a.size();
  }
  return text;
}
} // namespace
void View::buildSheet(const UI &u) {
  scene.clear();
  ensureStrips();
  pendingLoads = false;
  // The column the header, rows and notes use: wide over a running game.
  const bool wide = u.inGame;
  const auto G = sheetGeom(wide);

  // What the screen shows, filled in by state below, then drawn.
  struct Row {
    enum Kind { Nav, Choice, Toggle, Action, Primary, Danger, Info, Folder, Zip, Plain } kind = Nav;
    std::string label, value, detail;
    bool on = false;
  };
  std::string section, title, subtitle;
  std::vector<Row> rows;
  int focusRow = -1, first = 0, shown = sheetRows, rowH = G.rowH;
  bool menuPage = false; // the first page over a running game
  std::vector<std::string> notes, body;
  int bodyFirst = 0, bodyLines = 12;
  std::vector<Hint> left, right;
  int topFocus = -1;
  enum Special { NoSpecial, About, ControlsTest, ControlsPick, ControlsPad, ControlsMap } special = NoSpecial;
  // Labels and values are given in English and shown translated.
  auto row = [&](Row::Kind kind, const std::string &label, const std::string &value = "", bool on = false) {
    Row r;
    r.kind = kind;
    r.label = tr(label);
    r.value = tr(value);
    r.on = on;
    rows.push_back(r);
  };
  const Hint cross{"btn-cross-26", tr("Select"), Button::Cross}, back{"btn-circle-26", tr("Back"), Button::Circle};
  auto hint = [](const char *glyph, const std::string &text, Button b) { return Hint{glyph, tr(text), b}; };
  auto values = [&]() -> const Json::object_type & { return u.displayValues.as_object(); };
  auto boolean = [&](const char *name, bool fallback) {
    auto it = values().find(name);
    return it != values().end() && it->second.is_boolean() ? it->second.as_boolean() : fallback;
  };

  // Display, metrics and RTP pages are shared by settings and game settings.
  auto displayPage = [&] {
    title = tr("Display");
    subtitle = u.gameOptions ? u.gameEntry.name : tr("Default for every game");
    const char *keys[] = {"fixedAspectRatio", "integerScalingActive", "smoothScaling"};
    const char *labels[] = {"Aspect ratio", "Scaling", "Filtering"};
    const char *names[][2] = {{"Fill screen", "Original"}, {"Fit screen", "Whole pixels"}};
    for (int i = 0; i < 3; ++i) {
      auto entry = values().find(keys[i]);
      int value = i == 0 ? 1 : i == 2 ? 1 /* Sharp */ : 0;
      bool custom = false;
      if (entry != values().end()) {
        auto &v = entry->second;
        if (i == 2) {
          value = filterIndex(v);
          custom = value < 0;
        } else {
          custom = !v.is_boolean();
          if (!custom)
            value = v.as_boolean();
        }
      }
      row(Row::Choice, labels[i], custom ? "Custom" : i == 2 ? filterNames[value] : names[i][value]);
    }
    auto depth = values().find("textureDepth");
    bool full = depth != values().end() && depth->second.is_integer() && depth->second.as_integer() == 32;
    row(Row::Choice, "Image quality", full ? "32-bit (more memory)" : "16-bit (recommended)");
    focusRow = u.setting;
    // What each choice of the focused row gives and costs.
    if (u.setting == 0)
      notes = {tr("Original: the game's own shape, nothing stretched. Black borders fill the sides."),
               tr("Fill screen: no borders, the whole screen is used, but the picture is stretched wider, so round "
                  "things look oval.")};
    else if (u.setting == 1)
      notes = {tr("Whole pixels: the sharpest picture. Every game pixel is drawn the same size, so lines and text stay "
                  "even and nothing shimmers when the map scrolls. The picture is smaller, with wider borders."),
               tr("Fit screen: the biggest picture, but game pixels end up slightly different sizes, so thin lines and "
                  "small text can look uneven.")};
    else if (u.setting == 2)
      notes = {tr("Sharp: crisp pixels with only their edges smoothed. Hides uneven pixels with Fit screen and stays clear."),
               tr("Nearest: hard, blocky pixels. Sharpest with Whole pixels; with Fit screen some pixels are wider than others."),
               tr("Bilinear: soft and smooth, hides uneven pixels, but the whole picture looks blurry.")};
    if (u.setting == 3)
      notes = {tr("16-bit keeps game pictures in half the video memory, with a fine dither that is hard to see on the "
                  "Vita screen. Recommended for every game."),
               tr("32-bit keeps every colour exact but needs twice the memory: bigger games can run out of it and "
                  "crash. Only use it if a game looks wrong in 16-bit."),
               tr(u.gameOptions ? "Saved for this game; applies the next time it starts."
                                : "Used by games without settings of their own. Applies the next time a game starts.")};
    else
      notes.push_back(tr(u.gameOptions ? "Saved for this game. <square> Square goes back to the default for every game."
                                       : "Used by games without settings of their own."));
    left = {hint("btn-cross-26", "Change", Button::Cross), hint("btn-square-26", u.gameOptions ? "Use default" : "Reset", Button::Square), back};
  };
  auto metricsPage = [&] {
    title = tr("Performance and speed");
    subtitle = u.gameOptions ? u.gameEntry.name : tr("Default for every game");
    int mode = 0;
    auto i = values().find("performanceMetrics");
    if (i != values().end() && i->second.is_integer() && i->second.as_integer() >= 0 && i->second.as_integer() <= 3)
      mode = i->second.as_integer();
    const char *modes[] = {"Off", "FPS", "FPS and frame timing", "Full stats"};
    row(Row::Choice, "Overlay in game", modes[mode]);
    row(Row::Toggle, "Fast forward with <l>+<r>", "", boolean("fastForward", true));
    if (u.metricsRows() > 2) {
      row(Row::Toggle, "Crash rescue", "", boolean("crashRescue", true));
      row(Row::Toggle, "Save backups", "", boolean("saveBackups", true));
    }
    focusRow = u.setting;
    if (u.setting == 3)
      notes = {tr("Keeps the save a game replaces, outside the game folder, so an overwritten save can be put back and "
                  "removing a game keeps its saves."),
               tr(u.gameOptions && !u.inGame ? "The saves are on this game's page, Saves." : "The saves are in Settings, Save backups.")};
    else if (u.setting == 2)
      notes = {tr("Keeps a quick copy of the game at map changes and battle starts, where crashes usually happen. "
                  "If the game stops (an error, a closed app, a power loss), starting it again offers to resume there."),
               tr(u.inGame ? "Your save files are never changed. Saving in the game drops the copy."
                           : "Your save files are never changed. Saving in the game drops the copy. Applies the next time a game starts.")};
    else if (u.setting == 1)
      notes = {tr("<l>+<r> switches up to twice the speed; »» shows in the corner while it is on. Busy maps run as fast "
                  "as they can."),
               tr(u.inGame ? "Any button can also be mapped to Fast forward in Controls."
                           : "Any button can also be mapped to Fast forward in Controls. Applies the next time a game starts.")};
    else if (mode == 3)
      notes = {tr("Full stats add how busy the game's main thread is, the CPU and GPU speeds, memory and video memory, "
                  "and the battery: charge, time left, temperature and an estimate of the power drawn."),
               tr("The power estimate shows after about a minute away from the charger.")};
    else
      notes = {tr("FPS: frames shown each second. MS: average time between frames, waits included. Peak: the longest frame "
                  "in about the last five seconds."),
               tr(u.inGame ? "Drawn in an empty border when there is one, otherwise in a corner."
                           : "Drawn in an empty border when there is one, otherwise in a corner. Applies the next time a game starts.")};
    left = {hint("btn-cross-26", "Change", Button::Cross), hint("btn-square-26", u.gameOptions ? "Use default" : "Off", Button::Square), back};
  };
  auto rtpPage = [&] {
    title = tr("RTP locations");
    subtitle = tr("Shared by every game");
    rowH = rtpRowH;
    shown = rtpPackCount;
    for (int i = 0; i < rtpPackCount; ++i) {
      auto entry = u.rtpChoices.as_object().find(packs[i]);
      Row r;
      r.kind = Row::Nav;
      r.label = packLabel(packs[i]) + " RTP";
      r.value = i < int(u.rtpStatuses.size()) ? tr(u.rtpStatuses[i]) : "";
      r.detail = entry == u.rtpChoices.as_object().end() ? u.model.paths.at(std::string("rtp/") + packs[i]) + " " + tr("(automatic)")
                                                         : entry->second.as_string();
      r.detail = tail(r.detail, 84);
      rows.push_back(r);
    }
    focusRow = u.rtpSelected;
    notes = {tr("An RTP in a ZIP is unpacked once; the original pack is kept.")};
    left = {hint("btn-cross-26", "Choose", Button::Cross), hint("btn-square-26", "Automatic", Button::Square),
            hint("btn-triangle-26", "Check again", Button::Triangle), back};
  };

  if (!u.releaseReport.empty()) {
    section = tr("Update");
    title = "HardRPG " + u.releaseNotice.tag;
    subtitle = tr("A newer release is out");
    body = u.releaseReport;
    bodyFirst = u.releaseScroll;
    bodyLines = 11;
    left = {hint("btn-cross-26", "Continue", Button::Cross)};
    right = {{"UP DOWN", tr("Scroll"), Button::None}};
  } else if (u.page == Page::Controls) {
    section = tr("Game settings");
    subtitle = u.controlsEntry.name;
    if (u.controlsTesting) {
      special = ControlsTest;
      title = tr("Test controls");
      subtitle = tr("Press buttons, move the sticks, touch the screen or the back");
      left = {hint("btn-circle-26", "Stop testing", Button::Circle)};
    } else if (u.controlPicking) {
      special = ControlsPick;
      title = u.controlLabel(u.setting);
      subtitle = trf("now does: {}", {u.controlValue(u.setting)});
      notes = {tr("Top row: RPG Maker actions (X=A: RPG Maker's X, key A on PC). Below: keys. Dimmed: in use.")};
      left = {hint("btn-cross-26", "Assign", Button::Cross), hint("btn-circle-26", "Cancel", Button::Circle)};
      if (!u.controlsAdvanced && u.setting >= 1 && u.setting <= 12)
        left.push_back(hint("btn-square-26", "Turbo on/off", Button::Square));
      right = {{nullptr, tr("Tap twice to assign"), Button::None}};
    } else if (u.controlsPad) {
      special = ControlsPad;
      bool rear = u.controlsPad == 2;
      title = tr(rear ? "Rear touch pad" : "Front touch screen");
      int mode = rear ? u.controls.rear : u.controls.front;
      notes = {!u.message.empty() ? u.message
               : tr(rear ? (mode == 1 ? "Two zones along the top edge, clear of your grip."
                                      : "Fingers already on the pad when the game starts are ignored.")
                    : mode == 1 || mode == 3 ? "These buttons are drawn on screen. Whole screen, halves and corners are invisible."
                    : mode >= 4 ? "Invisible zones: nothing is drawn. Several fingers press several zones."
                                : "Whole screen, halves and corners are invisible touch zones.")};
      left = {hint("btn-cross-26", "Map zone", Button::Cross), back};
      right = {{nullptr, tr("Left and right: mode or zone"), Button::None}};
    } else if (u.controlsMap()) {
      special = ControlsMap;
      title = tr("Controls");
      left = {hint("btn-cross-26", "Change", Button::Cross), back};
      right = {{nullptr, tr("D-pad: move around the picture"), Button::None}};
    } else {
      title = tr(u.controlsAdvanced ? "Sticks and pointer" : "Controls");
      for (int i = 0; i < u.controlsCount(); ++i) {
        bool picks = u.controlsAdvanced ? i >= 1 && i <= 4 : i >= 1 && i <= 12;
        row(picks ? Row::Nav : Row::Choice, u.controlLabel(i), u.controlValue(i));
      }
      focusRow = u.setting;
      first = u.settingFirst;
      notes = {!u.message.empty() ? u.message
               : u.controlsAdvanced && u.setting == 0
                   ? tr("Same as left stick: the right stick moves like the left one. Mouse pointer: it moves the "
                        "pointer, for games played with the mouse. Or map each direction below to any action or key.")
               : u.controlsAdvanced && u.setting <= 4
                   ? tr("<cross> Cross gives this direction an RPG Maker action or a keyboard key. <square> Square: same as the left stick again.")
               : u.controlsAdvanced && u.setting == 8
                   ? tr("How fast turbo buttons repeat. Turn turbo on for a button with <square> Square in its key "
                        "list, or map a button to Turbo and press it with the button to repeat.")
               : tr(u.controlsAdvanced ? "Pointer options need a game that reads the mouse."
                    : u.setting >= 1 && u.setting <= 12 ? "<cross> Cross gives this button a keyboard key or an RPG Maker action."
                    : u.controls.enabled ? "Each button does what is shown. Saved for this game."
                                         : "Pick a button and press <cross> Cross to map it to any keyboard key.")};
      left = {cross, back};
      if (u.controlsAdvanced && u.setting <= 4)
        left = {cross, hint("btn-square-26", "Mirror left stick", Button::Square), back};
    }
  } else if (!u.errorReport.empty()) {
    section = tr("Error report");
    const auto &first = u.errorReport.front();
    title = first.size() > 11 && first.compare(0, 11, "Exception: ") == 0
                ? first.substr(11)
                : tr("The game ended with an error");
    body = u.errorReport;
    bodyFirst = u.errorScroll;
    left = {hint("btn-cross-26", "Back", Button::Cross), hint("btn-square-26", "Save report", Button::Square)};
    right = {{"UP DOWN", tr("Scroll"), Button::None}};
  } else if (!u.missingRtp.empty()) {
    section = tr("Can't start the game");
    title = trf("Missing {} RTP", {packLabel(u.missingRtp)});
    subtitle = tr("This game needs the RTP's shared graphics and sounds.");
    row(Row::Primary, "Choose an RTP folder or ZIP");
    row(Row::Plain, "Back to games");
    focusRow = 0;
    notes = {trf("Looked in: {}", {u.model.paths.at("rtp/" + u.missingRtp)})};
    left = {hint("btn-cross-26", "Choose RTP", Button::Cross), back};
  } else if (u.browser) {
    bool rtp = u.browseMode == BrowseMode::Rtp, folder = u.browseMode == BrowseMode::Folder;
    section = tr(rtp ? "RTP locations" : folder ? "Game folders" : "Library");
    title = tr(rtp ? "Choose an RTP" : folder ? "Choose a game folder" : "Add games");
    subtitle = tail(u.browser->path.empty() ? tr("Choose a storage device") : u.browser->path, 70);
    rowH = browserRowH;
    shown = browserRows;
    for (auto &f : u.browser->list) {
      row(f.directory ? Row::Folder : Row::Zip, "");
      rows.back().label = f.name; // file names stay as they are
    }
    focusRow = u.browserSelected;
    first = u.browserFirst;
    if (u.browser->list.empty())
      notes = {tr(u.browser->path.empty() ? "No storage found."
                  : rtp || folder        ? "No folders here. <square> Square uses this folder."
                                         : "No folders here. <square> Square looks for games in this folder.")};
    else if (!rtp && !folder)
      notes = {tr("<square> Square looks for games in the folder shown above, including the folders inside it. Games stay where they are.")};
    left = {hint("btn-cross-26", "Open", Button::Cross),
            hint("btn-square-26", rtp || folder ? "Use this folder" : "Add games here", Button::Square), back};
    right = {{"START", tr("Close"), Button::Start}};
  } else if (u.savesPage()) {
    // Save backups (save_backups.h): a game's saves, one save's versions,
    // a question before anything is replaced or deleted, and Settings' list.
    rowH = rtpRowH;
    shown = 5;
    first = u.settingFirst;
    focusRow = u.setting;
    auto played = [](int64_t seconds) {
      char text[24];
      std::snprintf(text, sizeof text, "%d:%02d", int(seconds / 3600), int(seconds / 60 % 60));
      return trf("{} played", {text});
    };
    auto describe = [&](const SaveVersion &v, bool when) {
      std::vector<std::string> parts;
      if (when)
        parts.push_back(backupTime(v.saved));
      if (!v.mapName.empty())
        parts.push_back(v.mapName);
      if (!v.leader.empty())
        parts.push_back(v.level > 0 ? trf("{}, level {}", {v.leader, std::to_string(v.level)}) : v.leader);
      if (v.playtime > 0)
        parts.push_back(played(v.playtime));
      std::string out;
      for (auto &p : parts)
        out += (out.empty() ? "" : " \xc2\xb7 ") + p;
      return out;
    };
    auto why = [](const std::string &w) {
      return tr(w == "save"      ? "Replaced by a save in the game"
                : w == "manual"  ? "Backed up by hand"
                : w == "restore" ? "Kept before a restore"
                : w == "removed" ? "Kept when the game was removed"
                                 : "Copied by the launcher");
    };
    auto slotName = [](const SaveSlot &slot) {
      return slot.number >= 0 ? trf("Slot {}", {std::to_string(slot.number)}) : slot.name;
    };
    auto count = [](size_t n) { return n == 1 ? tr("1 backup") : trf("{} backups", {std::to_string(n)}); };
    const auto &g = u.backupGame;
    section = tr(u.savesFromSettings || u.page == Page::Backups ? "Save backups" : "Game settings");
    if (u.page == Page::Backups) {
      section = tr("Settings");
      title = tr("Save backups");
      int64_t total = 0;
      for (auto &b : u.backupGames)
        total += b.bytes;
      subtitle = u.backupGames.empty() ? "" : trf("{} on the memory card", {backupSize(total)});
      for (auto &b : u.backupGames) {
        row(Row::Nav, "", b.installed ? backupSize(b.bytes) : trf("Not in library \xc2\xb7 {}", {backupSize(b.bytes)}));
        rows.back().label = b.name;
        rows.back().detail = b.installed ? trf("{} saves \xc2\xb7 newest {}", {std::to_string(b.slots), backupTime(b.newest)})
                                         : trf("Was in {}", {tail(b.folder, 70)});
      }
      row(Row::Choice, "Versions kept per save", std::to_string(u.backupKeep));
      notes = {tr(u.backupGames.empty() ? "No backups yet. A copy is kept each time a game replaces a save."
                                        : "Games removed from the library keep their saves here. Add the game back in "
                                          "the same folder and they return to it.")};
      left = {u.setting < int(u.backupGames.size()) ? hint("btn-cross-26", "Open", Button::Cross)
                                                    : hint("btn-cross-26", "Change", Button::Cross),
              back};
    } else if (u.page == Page::Saves) {
      title = tr("Saves");
      subtitle = g.name;
      for (auto &r : u.saveRows()) {
        if (r.first == UI::AdoptRow) {
          row(Row::Action, "Use saves from an earlier copy");
          rows.back().detail = tail(u.backups.game(u.adoptFrom).folder, 70);
        } else if (r.first == UI::SlotRow) {
          auto &slot = u.saveSlots[r.second];
          size_t kept = 0;
          for (auto &v : slot.versions)
            kept += !(slot.present && v.saved == slot.now.saved && v.size == slot.now.size);
          bool current = kept < slot.versions.size();
          row(Row::Nav, "", !slot.present ? tr("Backup only")
                            : kept       ? count(kept)
                            : current    ? tr("Backed up")
                                         : tr("No backup yet"));
          rows.back().label = slotName(slot);
          rows.back().detail = slot.present ? describe(slot.now, true)
                               : g.installed ? tr("Missing from the game")
                                             : describe(slot.versions.front(), true);
        } else if (r.first == UI::BackUpRow)
          row(Row::Action, "Back up now");
        else if (r.first == UI::RestoreMissingRow) {
          row(Row::Action, "");
          rows.back().label = trf("Restore missing saves ({})", {std::to_string(r.second)});
        } else
          row(Row::Danger, "Delete all backups of this game");
      }
      if (u.saveSlots.empty())
        notes.push_back(tr("No saves found yet. A copy is kept each time the game replaces a save."));
      if (g.installed)
        notes.push_back(tr("A copy of a save is kept each time the game replaces it, and when you come back to the library."));
      else
        notes.push_back(tr("This game is not in the library. Add it back in the same folder and its saves return to it."));
      left = {hint("btn-cross-26", "Open", Button::Cross), back};
    } else if (u.page == Page::SaveVersions && u.slotIndex < int(u.saveSlots.size())) {
      auto &slot = u.saveSlots[u.slotIndex];
      title = slotName(slot);
      subtitle = slot.present ? trf("Now: {}", {describe(slot.now, true)}) : tr("Missing from the game");
      auto list = u.shownVersions();
      for (auto &v : list) {
        row(Row::Nav, "", backupSize(v.size));
        rows.back().label = backupTime(v.saved);
        auto info = describe(v, false);
        rows.back().detail = why(v.why) + (info.empty() ? "" : " \xc2\xb7 " + info);
      }
      if (list.empty())
        notes = {tr("No earlier versions of this save are kept yet.")};
      else if (!g.installed)
        notes = {tr("Add the game to the library again to restore its saves.")};
      left = {hint("btn-cross-26", "Restore", Button::Cross), hint("btn-square-26", "Delete", Button::Square), back};
    } else if (u.page == Page::SaveConfirm) {
      rowH = sheetRowH;
      shown = sheetRows;
      first = 0;
      auto q = u.saveQuestion;
      const SaveSlot *slot = u.slotIndex < int(u.saveSlots.size()) ? &u.saveSlots[u.slotIndex] : nullptr;
      auto list = u.shownVersions();
      const SaveVersion *v = u.versionIndex < int(list.size()) ? &list[u.versionIndex] : nullptr;
      subtitle = slot && (q == UI::RestoreVersion || q == UI::DeleteVersion) ? slotName(*slot) + " \xc2\xb7 " + g.name : g.name;
      const char *yes = "Restore";
      if (q == UI::RestoreVersion) {
        title = tr(u.restoreReplacesNewer() ? "Replace a newer save?" : "Restore this save?");
        if (slot && slot->present)
          notes.push_back(trf(u.restoreReplacesNewer() ? "In the game now (newer): {}" : "In the game now: {}",
                              {describe(slot->now, true)}));
        if (v)
          notes.push_back(trf("Backup to restore: {}", {describe(*v, true)}));
        if (slot && slot->present)
          notes.push_back(tr("The save in the game is kept as a backup, so you can switch back."));
      } else if (q == UI::DeleteVersion) {
        title = tr("Delete this backup?");
        yes = "Delete";
        if (v)
          notes = {describe(*v, true), tr("This cannot be undone.")};
      } else if (q == UI::ForgetGame) {
        title = tr("Delete all backups of this game?");
        yes = "Delete all";
        notes = {trf("{} saves \xc2\xb7 {}", {std::to_string(g.slots), backupSize(g.bytes)}), tr("This cannot be undone.")};
      } else if (q == UI::RestoreMissing) {
        title = tr("Restore the missing saves?");
        notes = {tr("The newest backup of each save the game does not have goes back into the game.")};
      } else {
        title = tr("Use the saves from an earlier copy?");
        yes = "Use them";
        auto other = u.backups.game(u.adoptFrom);
        notes = {trf("Backups of {} from {}.", {other.name, tail(other.folder, 60)}),
                 tr("They move to this copy of the game.")};
      }
      row(Row::Plain, "Cancel");
      row(q == UI::DeleteVersion || q == UI::ForgetGame ? Row::Danger : Row::Primary, yes);
      left = {hint("btn-cross-26", "Choose", Button::Cross), hint("btn-circle-26", "Cancel", Button::Circle)};
    }
  } else if (u.gameOptions) {
    section = tr("Game settings");
    topFocus = -1;
    if (u.page == Page::Game) {
      title = u.gameEntry.name;
      subtitle = engineLong(u.gameEntry.version);
      const char *names[] = {"Controls", "Display", "Audio and MIDI", "Performance and speed", "RTP locations",
                             "Rename", "Game information", "Saves", "Play", "Remove from library", "Delete the ZIP, keep the game"};
      Row::Kind kinds[] = {Row::Nav, Row::Nav, Row::Nav, Row::Nav, Row::Nav, Row::Action, Row::Nav, Row::Nav, Row::Primary, Row::Danger, Row::Danger};
      // Over a running game: resume, the settings that apply at once, leave.
      const char *playing[] = {"Resume", "Controls", "Display", "Audio", "Performance and speed", "Cheats", "Back to library"};
      // Resume is a row like the others: only the focus is ruby.
      Row::Kind playingKinds[] = {Row::Plain, Row::Nav, Row::Nav, Row::Nav, Row::Nav, Row::Nav, Row::Plain};
      for (int i = 0; i < u.gameOptionsCount(); ++i) {
        // Without cheats the last row is Back to library.
        int at = u.inGame && !u.cheat && i == 5 ? 6 : i;
        row(u.inGame ? playingKinds[at] : kinds[i], u.inGame ? playing[at] : names[i]);
      }
      focusRow = u.setting;
      first = u.settingFirst;
      if (u.inGame) {
        section = tr("Paused");
        menuPage = true;
        rowH = inGameMenuRowHeight(u.gameOptionsCount());
        notes = {tr("Changes apply at once and are saved for this game. Reopen: hold <l>+<r>+<start>, or tap the icon in the empty border.")};
        left = {hint("btn-cross-26", "Open", Button::Cross), hint("btn-circle-26", "Resume", Button::Circle)};
      } else
        left = {hint("btn-cross-26", "Open", Button::Cross), back};
    } else if (u.page == Page::Cheats) {
      section = tr("Cheats");
      title = tr(u.cheatTitle.empty() ? "Cheats" : u.cheatTitle);
      subtitle = u.cheatSubtitle;
      const bool editing = u.cheatEditRow >= 0 && size_t(u.cheatEditRow) < u.cheatRows.size();
      for (size_t i = 0; i < u.cheatRows.size(); ++i) {
        auto &c = u.cheatRows[i];
        Row::Kind kind = c.kind == "nav"       ? Row::Nav
                         // On/off cheats say "On" or "Off": clearer than a switch graphic.
                         : c.kind == "toggle"  ? Row::Action
                         : c.kind == "choice"  ? Row::Choice // left/right arrows
                         // A number row opens the editor: its value and ›, like a page.
                         : c.kind == "number"  ? Row::Nav
                         : c.kind == "primary" ? Row::Primary
                         : c.kind == "danger"  ? Row::Danger
                         : c.kind == "plain"   ? Row::Info
                                               : Row::Action;
        // The row being typed shows the editor's value, its digit marked.
        // The row keeps its value while the editor panel shows the new one
        // (a row layer per typed value would fill video memory).
        const std::string value = c.kind == "toggle" ? (c.on ? "On" : "Off") : c.value;
        if (c.raw) {
          Row r;
          r.kind = kind;
          r.label = c.label;
          r.value = tr(value); // "On"/"Off"; numbers pass through

          r.on = c.on;
          rows.push_back(r);
        } else
          row(kind, c.label, value, c.on);
      }
      focusRow = editing ? u.cheatEditRow : u.setting;
      first = u.settingFirst;
      const UI::CheatRow *focused =
          u.setting >= 0 && size_t(u.setting) < u.cheatRows.size() ? &u.cheatRows[size_t(u.setting)] : nullptr;
      if (editing) {
        auto &r = u.cheatRows[size_t(u.cheatEditRow)];
        notes = {tr("<up><down> change the lit digit, <left><right> move to another digit, <cross> sets the number.")};
        left = {hint("btn-cross-26", "Set", Button::Cross), hint("btn-circle-26", "Cancel", Button::Circle),
                hint("btn-square-26", "Maximum", Button::Square)};
        if (r.lo < 0)
          left.push_back(hint("btn-triangle-26", "Minus", Button::Triangle));
        right = {{"UP DOWN", tr("Digit"), Button::None}, {"LEFT RIGHT", tr("Move"), Button::None}};
      } else {
        // A message shows over the hint bar; the note stays the row's own.
        if (focused && !focused->note.empty())
          notes = {fillArgs(tr(focused->note), focused->noteArgs)};
        else
          notes = {tr("Cheats change this game while it runs. Save in the game to keep what they gave you.")};
        const char *use = !focused                    ? "Use"
                          : focused->editable()       ? "Type a number"
                          : focused->kind == "nav"    ? "Open"
                          : focused->kind == "toggle" ? "On/off"
                          : focused->kind == "choice" ? "Next"
                          : focused->kind == "plain"  ? nullptr
                                                      : "Use";
        left.clear();
        if (use)
          left.push_back(hint("btn-cross-26", use, Button::Cross));
        if (focused && !focused->square.empty())
          left.push_back(hint("btn-square-26", focused->square.c_str(), Button::Square));
        left.push_back(back);
        if (focused && focused->kind == "choice")
          right = {{"LEFT RIGHT", tr("Change"), Button::None}};
      }
    } else if (u.page == Page::Audio) {
      title = tr(u.inGame ? "Audio" : "Audio and MIDI");
      subtitle = u.gameEntry.name;
      auto boost = values().find("audioBoost");
      int level = boost != values().end() ? audioBoostValues[audioBoostIndex(boost->second)] : 100;
      row(Row::Choice, "Volume boost", level == 100 ? "Off" : "");
      if (level != 100)
        rows.back().value = std::to_string(level) + "%";
      // Sound and the MIDI effects need a restart, so they stay in the library.
      if (!u.inGame) {
        const char *keys[] = {"enableAudio", "midiReverb", "midiChorus"};
        const char *names[] = {"Sound", "MIDI reverb", "MIDI chorus"};
        for (int i = 0; i < 3; ++i)
          row(Row::Toggle, names[i], "", boolean(keys[i], i == 0));
      }
      focusRow = u.setting;
      if (u.setting == 0)
        notes = {tr("Plays this game louder than its own maximum, for games that stay quiet even at full Vita volume."),
                 tr(u.inGame ? "Saved for this game and applied now. The loudest sounds are softened instead of crackling."
                             : "Saved for this game. The loudest sounds are softened instead of crackling.")};
      else
        notes = {tr("Saved for this game; applies the next time it starts."), tr("MIDI effects cost CPU time and are off by default.")};
      left = {hint("btn-cross-26", "Change", Button::Cross), hint("btn-square-26", "Use default", Button::Square), back};
    } else if (u.page == Page::Rescue) {
      const auto &r = u.rescueInfo;
      auto span = [](int64_t seconds) {
        seconds = std::max<int64_t>(0, seconds);
        return seconds < 90     ? trf("{} s", {std::to_string(seconds)})
               : seconds < 5400 ? trf("{} min", {std::to_string((seconds + 30) / 60)})
                                : trf("{} h", {std::to_string((seconds + 1800) / 3600)});
      };
      const char *moment = r.reason == "transfer" ? "a map change"
                           : r.reason == "battle" ? "the start of a battle"
                           : r.reason == "menu"   ? "opening the menu"
                                                  : "returning to the map";
      const int64_t since = r.writtenAt > 0 ? int64_t(std::time(nullptr)) - r.writtenAt : -1;
      section = tr(r.left ? "Resume where you left" : "Resume from crash");
      title = u.gameEntry.name;
      if (r.left)
        subtitle = since >= 0 ? trf("You left this game. A quick save from {} ago is ready.", {span(since)})
                              : tr("You left this game. A quick save is ready.");
      else if (!r.error.empty())
        subtitle = trf("An error ({}) stopped this game. A quick save from {} before it is ready.", {r.error, span(r.age)});
      else
        subtitle = since >= 0 ? trf("This game stopped without saving (closed, or the Vita lost power). A quick save from {} ago is ready.", {span(since)})
                              : tr("This game stopped without saving (closed, or the Vita lost power). A quick save is ready.");
      row(Row::Primary, "Resume from the quick save");
      row(Row::Plain, "Start normally");
      row(Row::Danger, "Discard the quick save");
      focusRow = u.setting;
      std::string place = r.mapName.empty() ? trf("map {}", {std::to_string(r.mapId)})
                                            : trf("map {} ({})", {std::to_string(r.mapId), r.mapName});
      notes = {trf("Made at {}, {}. Play time {}.", {tr(moment), place, span(r.playtime)})};
      if (!r.leader.empty())
        notes.push_back(r.level > 0 ? trf("Party leader: {}, level {}.", {r.leader, std::to_string(r.level)})
                                    : trf("Party leader: {}.", {r.leader}));
      notes.push_back(tr("Your save files are not changed. Save in the game soon after resuming."));
      if (r.saveDisabled)
        notes.push_back(tr("Saving was turned off at that point of the game."));
      if (!r.error.empty() && since >= 0)
        notes.push_back(trf("The error happened {} ago.", {span(since)}));
      left = {hint("btn-cross-26", "Choose", Button::Cross), hint("btn-circle-26", "Cancel", Button::Circle)};
    } else if (u.page == Page::Remove) {
      auto q = u.removeQuestions[u.removeStep];
      bool source = !u.removeInfo.folder.empty() || !u.removeInfo.zip.empty();
      bool copy = q == UI::DeleteUnpacked || (!source && u.gameEntry.node.archive);
      bool zipOnly = q == UI::DeleteZipOnly;
      section = trf("Remove {}", {u.gameEntry.name});
      title = tr(zipOnly ? "Delete the ZIP to free its space?" : copy ? "Delete its unpacked copy too?" : "Delete its game files too?");
      subtitle = tr(zipOnly ? "The game stays listed and plays from its unpacked copy."
                    : u.removeStep == 0 ? "It will be removed from the library."
                    : u.removeFiles ? "Game files: will be deleted." : "Game files: kept.");
      bool one = copy || zipOnly;
      row(Row::Plain, one ? "No, keep it" : "No, keep them");
      row(Row::Danger, one ? "Yes, delete it" : "Yes, delete them");
      focusRow = u.setting;
      notes = {tail(u.removePath(q), 90),
               tr(zipOnly ? "Saves and settings stay: they are in the unpacked copy."
                  : copy  ? "The unpacked copy holds the game's saves. This cannot be undone."
                          : "Saves kept inside the game files go with them. This cannot be undone.")};
      if (!zipOnly)
        notes.push_back(tr("A copy of its saves stays in Settings, Save backups."));
      left = {hint("btn-cross-26", "Answer", Button::Cross), hint("btn-circle-26", "Cancel", Button::Circle)};
    } else if (u.page == Page::GameInfo) {
      title = tr("Game information");
      subtitle = u.gameEntry.name;
      row(Row::Info, "Engine", engineLong(u.gameEntry.version));
      row(Row::Info, "Source", u.gameEntry.node.archive ? "ZIP archive" : "Game folder");
      auto location = u.gameEntry.node.path();
      if (u.gameEntry.node.archive && !u.gameEntry.node.inner.empty())
        location += "!/" + u.gameEntry.node.inner;
      row(Row::Info, "Location", tail(location, 60));
      if (u.gameEntry.node.archive) {
        auto cache = u.model.paths.at("cache/zip-" + key(u.gameEntry.node.identity(u.model.paths)));
        row(Row::Info, directory(cache) ? "Unpacked copy" : "Unpacks to", tail(cache, 56));
      } else {
        auto bank = values().find("midiSoundFont");
        row(Row::Info, "MIDI bank",
            bank != values().end() && bank->second.is_string() && !bank->second.as_string().empty() ? tail(bank->second.as_string(), 56)
                                                                                                      : "Bundled General MIDI bank");
      }
      // Facts downloaded with the artwork, when there are any.
      auto found = art.find(u.entryKey(u.gameEntry));
      if (found != art.end() && !found->second.info.empty()) {
        try {
          auto info = json5pp::parse(found->second.info);
          auto text = [&](const char *name) {
            auto &o = info.as_object();
            auto it = o.find(name);
            return it != o.end() && it->second.is_string() ? it->second.as_string() : std::string();
          };
          for (auto &f : {std::make_pair("Developer", "developer"), std::make_pair("Publisher", "publisher"),
                          std::make_pair("Released", "released"), std::make_pair("Genres", "genres")})
            if (!text(f.second).empty())
              row(Row::Info, f.first, tail(text(f.second), 60));
          if (!text("description").empty())
            notes.push_back(text("description"));
          if (!text("sources").empty())
            notes.push_back(trf("Details from {}.", {text("sources")}));
        } catch (...) {
        }
      }
      shown = 9;
      left = {back};
    } else if (u.page == Page::Display)
      displayPage();
    else if (u.page == Page::Metrics)
      metricsPage();
    else if (u.page == Page::Rtp)
      rtpPage();
  } else if (u.focus == Focus::About) {
    special = About;
    topFocus = 0;
    body.assign(std::begin(releaseNotes), std::end(releaseNotes));
    bodyFirst = u.aboutScroll;
    bodyLines = 9;
    left = {back};
    right = {{"UP DOWN", tr("Release notes"), Button::None}};
  } else {
    section = tr("Settings");
    topFocus = 1;
    if (u.page == Page::Updates) {
      title = tr("Release checks");
      row(Row::Toggle, "Check for new releases at startup", "", u.automaticUpdates);
      row(Row::Action, "Check now");
      row(Row::Nav, "Newer release notes", u.releaseNotice.tag.empty() ? "None found" : u.releaseNotice.tag);
      focusRow = u.setting;
      notes = {tr("Checks published Alpha releases. Nothing is shown when you are up to date or offline."),
               tr("Updates are downloaded and installed by hand.")};
      left = {cross, back};
    } else if (u.page == Page::Display)
      displayPage();
    else if (u.page == Page::Metrics)
      metricsPage();
    else if (u.page == Page::Rtp)
      rtpPage();
    else if (u.page == Page::Folders) {
      title = tr("Game folders");
      subtitle = trf("Default: {}", {tail(u.model.paths.at("games"), 48)});
      row(Row::Action, "Add another folder");
      for (auto &f : u.gameFolders) {
        row(Row::Folder, "");
        rows.back().label = tail(f, 70);
      }
      focusRow = u.setting;
      first = u.settingFirst;
      notes = {tr("Folders are searched for games each time HardRPG starts. Removing a folder here keeps its games and saves.")};
      left = {hint("btn-cross-26", "Browse", Button::Cross), hint("btn-square-26", "Remove", Button::Square), back};
    } else if (u.page == Page::Keys) {
      title = tr("Game info and artwork");
      subtitle = tr("Covers, backgrounds, logos and details such as the year, developer and description");
      // A key shows only its ends, so it is recognisable but not readable over a shoulder.
      auto masked = [](const std::string &key) {
        return key.empty() ? std::string("Add a key") : key.size() <= 8 ? std::string("Saved") : key.substr(0, 4) + "\xe2\x80\xa6" + key.substr(key.size() - 4);
      };
      row(Row::Toggle, "Download game info automatically", "", u.autoGameInfo);
      row(Row::Toggle, "Blur adult artwork", "", u.blurAdult);
      row(Row::Nav, "SteamGridDB", masked(u.readKey(0)));
      row(Row::Nav, "IGDB client ID", masked(u.readKey(1)));
      row(Row::Nav, "IGDB client secret", masked(u.readKey(2)));
      row(Row::Nav, "RAWG", masked(u.readKey(3)));
      row(Row::Info, "Steam, DLsite, VNDB, itch.io", "No key needed");
      focusRow = u.setting;
      if (u.setting == 0)
        notes = {tr("Each new game is looked up once, in the background, while HardRPG is open and online. "
                    "Triangle on a game looks it up again and lets you pick the pictures."),
                 tr("Your own pictures and music: put cover, hero (the background), logo (PNG or JPG) or theme.ogg in "
                    "ux0:/data/hardrpg/art/<game name>/. They are used instead of downloaded ones.")};
      else if (u.setting == 1)
        notes = {tr("Covers and backgrounds a source marks as adult are shown blurred in the library and the artwork chooser.")};
      else if (u.setting == 3 || u.setting == 4)
        notes = {tr("IGDB: release details, covers and screenshots. Free: sign in at dev.twitch.tv/console, register an "
                    "application (any name, redirect http://localhost, category Application Integration), then copy "
                    "its Client ID and make a New Secret."),
                 tr("Both are needed. A wrong pair is skipped, and the other sources still work.")};
      else
        notes = {tr("SteamGridDB: covers, backgrounds and logos for almost any game. Free key: steamgriddb.com, "
                    "Preferences, API."),
                 tr("RAWG: release dates, developers and genres. Free key: rawg.io/apidocs."),
                 tr("With no source knowing a game, its own title screen is used.")};
      left = {u.setting <= 1 ? hint("btn-cross-26", "Change", Button::Cross) : hint("btn-cross-26", "Type key", Button::Cross)};
      if (u.setting > 1)
        left.push_back(hint("btn-square-26", "Remove key", Button::Square));
      left.push_back(back);
    } else if (u.page == Page::Sound) {
      title = tr("Sound");
      subtitle = tr("The launcher's own sounds. Games keep their audio settings.");
      row(Row::Toggle, "Menu sounds", "", u.menuSounds);
      row(Row::Choice, "Menu sound volume", std::to_string(u.effectsVolume) + "%");
      row(Row::Toggle, "Title music", "", u.themeMusic);
      row(Row::Choice, "Title music volume", std::to_string(u.musicVolume) + "%");
      focusRow = u.setting;
      if (u.setting >= 2)
        notes = {tr("Plays the highlighted game's own title music, read from its files. Nothing is downloaded."),
                 tr("Each game's track is found once and kept, so it starts at once next time. Put a hardrpg-theme.ogg "
                    "(or .mp3 or .wav) in a game's folder to choose its music. MIDI titles stay silent.")};
      else
        notes = {tr("Sounds for moving, opening, going back and starting a game.")};
      left = {hint("btn-cross-26", "Change", Button::Cross), hint("btn-square-26", "Default", Button::Square), back};
    } else if (u.page == Page::Errors) {
      title = tr("Game error history");
      for (auto &record : u.errors) {
        row(Row::Nav, "", "");
        rows.back().label = tr(record.game);
        rows.back().value = record.timestamp;
      }
      focusRow = u.setting;
      first = u.settingFirst;
      if (u.errors.empty())
        notes = {tr("No game has ended with an error.")};
      left = {hint("btn-cross-26", "View", Button::Cross), hint("btn-square-26", "Save report", Button::Square), back};
    } else {
      title = tr("Settings");
      row(Row::Nav, "Game display");
      row(Row::Nav, "RTP locations");
      row(Row::Nav, "Game folders", u.gameFolders.empty() ? "" : trf("{} added", {std::to_string(u.gameFolders.size())}));
      row(Row::Nav, "Game error history", u.errors.empty() ? "" : std::to_string(u.errors.size()));
      row(Row::Nav, "Performance and speed");
      row(Row::Nav, "Release checks", u.automaticUpdates ? "At startup" : "Off");
      int keys = !u.readKey(0).empty() + (!u.readKey(1).empty() && !u.readKey(2).empty()) + !u.readKey(3).empty();
      row(Row::Nav, "Game info and artwork", trf("{} sources", {std::to_string(4 + keys)}));
      {
        const char *views[] = {"Last used", "Carousel", "Grid", "List"};
        row(Row::Choice, "Library opens in", views[u.startView + 1]);
      }
      row(Row::Choice, "Language", languageName(language()));
      row(Row::Nav, "Welcome guide");
      row(Row::Nav, "Sound", u.menuSounds && u.themeMusic ? "On"
                             : u.menuSounds              ? "Menu sounds only"
                             : u.themeMusic              ? "Title music only"
                                                         : "Off");
      row(Row::Nav, "Save backups");
      row(Row::Action, "Exit HardRPG");
      focusRow = u.setting;
      first = u.settingFirst;
      left = {cross, back};
    }
  }

  // Background: the last game shown on home, far back behind a dark veil.
  // Over a running game (overlay): only the veil, so the paused game shows
  // through it.
  if (overlay) {
    // Nothing: the engine dims the game on the GPU, which costs nothing
    // here (a full-screen fill is the slowest thing the CPU draws).
  } else {
    Quad q;
    q.color = ink;
    q.w = float(width), q.h = float(height);
    scene.push_back(q);
    if (heroShown) {
      Quad h;
      h.image = heroShown;
      h.w = float(width), h.h = float(height), h.alpha = 0.5f;
      scene.push_back(h);
    } else {
      Quad g;
      g.image = &glow;
      g.w = float(width), g.h = float(height * 0.75), g.alpha = 1;
      scene.push_back(g);
    }
    Quad veil;
    veil.color = ink;
    veil.w = float(width), veil.h = float(height), veil.alpha = 0.82f;
    scene.push_back(veil);
    Quad t;
    t.image = &tint;
    t.w = float(width), t.h = float(height);
    scene.push_back(t);
  }
  auto place = [&](const Image &img, double x, double y, double alpha = 1) {
    Quad q;
    q.image = &img;
    q.x = float(x), q.y = float(y), q.w = float(img.width), q.h = float(img.height), q.alpha = float(alpha);
    scene.push_back(q);
  };
  auto solidQuad = [&](Color c, double x, double y, double w, double h, double alpha, double radius = 0) {
    Quad q;
    q.color = c;
    q.x = float(x), q.y = float(y), q.w = float(w), q.h = float(h), q.alpha = float(alpha), q.radius = float(radius);
    scene.push_back(q);
  };
  topBar(u, topFocus);

  // Header: a small section name over the title, the subtitle after it.
  if (special != About) {
    std::string key = section + "|" + title + "|" + subtitle;
    if (beginLayer(headerLayer, key, width - G.x, 76)) {
      label(0, 0, upper(section), 12, faint);
      int tw = measure(title, 30, true);
      label(0, 16, title, 30, white, 255, true, -1, G.w + 40);
      if (!subtitle.empty()) {
        int sw = measure(subtitle, 15);
        if (tw + 18 + sw <= G.w + 40)
          label(tw + 18, 29, subtitle, 15, muted);
        else
          label(0, 54, subtitle, 15, muted, 255, false, -1, G.w + 40);
      }
      endLayer();
    }
    place(headerLayer.image, G.x, sheetTitleY);
  }

  // Each cached row is a texture. Over a running game the game owns most of
  // video memory, so the menu keeps far fewer. Trimmed before anything
  // this frame is placed: the scene points at these layers.
  const size_t cacheMax = overlay ? 48 : 300;
  if (rowCache.size() > cacheMax)
    for (auto it = rowCache.begin(); it != rowCache.end();)
      it = rowCache.size() > cacheMax / 2 ? rowCache.erase(it) : std::next(it);

  // Rows: cached text over a quad; the focused one brighter, with the ruby mark.
  int rowsShown = std::min(int(rows.size()) - first, shown);
  int box = rowH - 6;
  for (int i = 0; i < rowsShown; ++i) {
    auto &r = rows[first + i];
    int y = G.top + i * rowH;
    bool focused = first + i == focusRow;
    if (menuPage && focused) {
      // The menu over a game: see-through ruby, so the focus is clear
      // without a slab of red across the screen.
      solidQuad({0, 0, 0}, G.x, y, G.w, box, 0.38, 10);
      solidQuad(ruby, G.x, y, G.w, box, 0.42, 10);
    } else if (r.kind == Row::Primary)
      solidQuad(ruby, G.x, y, G.w, box, focused ? 1 : 0.7, wide ? 10 : 8);
    else if (r.kind != Row::Info || focused) {
      if (!(menuPage && focused)) {
      // Over a game each row is a clear button: a dark plate under the tint.
      if (wide)
        solidQuad({0, 0, 0}, G.x, y, G.w, box, 0.38, 10);
      solidQuad(white, G.x, y, G.w, box, focused ? (wide ? 0.2 : 0.16) : wide ? 0.1 : 0.06, wide ? 10 : 8);
      }
    }
    if (focused && r.kind != Row::Primary && !menuPage)
      solidQuad(ruby, G.x, y, wide ? 6 : 4, box, 1, wide ? 3 : 2);
    std::string key = std::to_string(int(r.kind)) + "|" + r.label + "|" + r.value + "|" + r.detail + "|" + std::to_string(r.on) + "|" +
                      std::to_string(box);
    auto &layer = rowCache[key];
    if (layer.key.empty()) {
      beginLayer(layer, key, G.w, box);
      int textX = 18;
      const char *glyphName = r.kind == Row::Folder ? "folder-26" : r.kind == Row::Zip ? "memcard-26" : r.kind == Row::Action && r.label == tr("Add another folder") ? "plus-31" : nullptr;
      if (glyphName) {
        const Image &g = icon(glyphName);
        blitScaled(g, 14, (box - 24) / 2, 24, 24);
        textX = 50;
      }
      int labelY = r.detail.empty() ? (box - (menuPage ? 29 : wide ? 25 : 22)) / 2 : 5;
      Color labelColor = r.kind == Row::Danger ? rubyHi : r.kind == Row::Info ? muted : white;
      int labelSize = r.kind == Row::Info ? 16 : menuPage ? 24 : wide ? 21 : 18;
      bool bold = r.kind == Row::Primary || (wide && r.kind != Row::Info);
      int right = G.w - 16;
      if (r.kind == Row::Toggle) {
        // A switch: ruby and to the right when on.
        rect(right - 44, (box - 24) / 2, 44, 24, r.on ? ruby : Color{80, 80, 86}, 255, 12);
        rect(r.on ? right - 22 : right - 42, (box - 24) / 2 + 2, 20, 20, white, 255, 10);
        right -= 58;
      } else if (r.kind == Row::Choice) {
        label(right, (box - 22) / 2, "\xe2\x80\xba", 20, muted, 255, false, 1);
        int vs = wide ? 19 : 17, vw = measure(r.value, vs);
        label(right - 20, (box - (wide ? 23 : 20)) / 2, r.value, vs, white, 255, false, 1);
        label(right - 30 - vw, (box - 22) / 2, "\xe2\x80\xb9", 20, muted, 255, false, 1);
        right -= vw + 46;
      } else if (r.kind == Row::Nav) {
        label(right, (box - 22) / 2, "\xe2\x80\xba", 20, muted, 255, false, 1);
        right -= 22;
        if (!r.value.empty()) {
          int vs = wide ? 18 : 15;
          label(right, (box - (vs + 5)) / 2, r.value, vs, wide ? soft : muted, 255, false, 1, 300);
          right -= std::min(300, measure(r.value, vs)) + 16;
        }
      } else if (r.kind == Row::Info) {
        label(right, (box - 20) / 2, r.value, 16, white, 255, false, 1, G.w - 200);
        right = std::max(180, right - std::min(G.w - 200, measure(r.value, 16)) - 20);
      } else if (!r.value.empty()) {
        // An action with a state, like a cheat switch (On/Off) or a saved spot.
        int vs = wide ? 18 : 15;
        label(right, (box - (vs + 5)) / 2, r.value, vs, wide ? soft : muted, 255, false, 1, 340);
        right -= std::min(340, measure(r.value, vs)) + 16;
      }
      label(textX, labelY, r.label, labelSize, labelColor, 255, bold, -1, std::max(60, right - textX - 8));
      if (!r.detail.empty())
        label(textX, labelY + 24, r.detail, 13, faint, 255, false, -1, G.w - textX - 16);
      endLayer();
    }
    place(layer.image, G.x, y);
  }
  if (int(rows.size()) > shown) {
    int trackH = shown * rowH - 6, thumb = std::max(24, trackH * shown / int(rows.size()));
    int offset = (trackH - thumb) * first / std::max(1, int(rows.size()) - shown);
    solidQuad(white, G.x + G.w + 10, G.top, 4, trackH, 0.1, 2);
    solidQuad(white, G.x + G.w + 10, G.top + offset, 4, thumb, 0.6, 2);
  }

  // Long text (reports, release notes): one cached layer per line.
  int y = rows.empty() ? G.top : G.top + rowsShown * rowH + 6;
  if (menuPage)
    y = inGameMenuFooterY - 2; // one line just above the hint bar
  if (special == About) {
    place(icon("emblem-72"), G.x, 84);
    place(icon("wordmark-46"), G.x + 90, 92);
    std::string key = std::string("about|") + languageCode(language());
    // The credits on one line, or two when the language makes them longer.
    std::string site = std::string("github.com/Asukate/mkxp-z-vita  \xc2\xb7  ") + tr("powered by mkxp-z and EasyRPG Player"),
                drawing = tr("Vita drawing by Tokyoship, CC BY 3.0"), dot = "  \xc2\xb7  ";
    bool oneLine = measure(site + dot + drawing, 13) <= G.w - 92;
    if (beginLayer(headerLayer, key, G.w, 80)) {
      label(0, 0, trf("Version {}", {HARDRPG_VERSION}), 16, white);
      label(0, 24, tr("Classic RPG games on the PS Vita. Made by Asukate."), 15, muted);
      if (oneLine)
        label(0, 46, site + dot + drawing, 13, faint);
      else {
        label(0, 46, site, 13, faint, 255, false, -1, G.w - 92);
        label(0, 63, drawing, 13, faint, 255, false, -1, G.w - 92);
      }
      endLayer();
    }
    place(headerLayer.image, G.x + 92, 144);
    y = oneLine ? 230 : 240;
    solidQuad(white, G.x, y - 8, G.w, 1, 0.12);
  }
  if (!body.empty()) {
    int lineH = 25;
    for (int i = 0; i < bodyLines && i + bodyFirst < int(body.size()); ++i) {
      auto &text = body[i + bodyFirst];
      std::string key = "body|" + text;
      auto &layer = rowCache[key];
      if (layer.key.empty()) {
        beginLayer(layer, key, G.w, lineH);
        label(0, 2, text, 16, soft, 255, false, -1, G.w);
        endLayer();
      }
      place(layer.image, G.x, y + i * lineH);
    }
    if (int(body.size()) > bodyLines) {
      int trackH = bodyLines * lineH, thumb = std::max(24, trackH * bodyLines / int(body.size()));
      int offset = (trackH - thumb) * bodyFirst / std::max(1, int(body.size()) - bodyLines);
      solidQuad(white, G.x + G.w + 10, y, 4, trackH, 0.1, 2);
      solidQuad(white, G.x + G.w + 10, y + offset, 4, thumb, 0.6, 2);
    }
    y += std::min(bodyLines, int(body.size())) * lineH + 6;
  }

  // Controls: the key picker, the touch zones and the input test keep the
  // geometry their touch handling uses.
  if (special == ControlsPick) {
    std::vector<int> taken = {u.controls.frontLeft, u.controls.frontRight, u.controls.frontBottomLeft, u.controls.frontBottomRight,
                              u.controls.rearLeft,  u.controls.rearRight,  u.controls.rearBottomLeft,  u.controls.rearBottomRight};
    taken.insert(taken.end(), u.controls.buttons.begin(), u.controls.buttons.end());
    for (int i = 0; i < controlPickCount; ++i) {
      int x, ky, w, h;
      controlPickRect(i, x, ky, w, h);
      bool here = i == u.controlPickKey, used = controlPicks[i].value &&
                                                std::find(taken.begin(), taken.end(), controlPicks[i].value) != taken.end();
      bool keyRow = controlIsKey(controlPicks[i].value);
      if (here)
        solidQuad(ruby, x, ky, w, h, 1, 6);
      else {
        // A dark plate under each key so it reads over the game.
        if (overlay)
          solidQuad({0, 0, 0}, x, ky, w, h, 0.42, 6);
        solidQuad(white, x, ky, w, h, used ? 0.05 : keyRow ? 0.1 : 0.16, 6);
      }
      // RPG Maker actions are translated; keyboard keys keep their names.
      std::string name = controlPicks[i].row ? controlPicks[i].name : trc("picker", controlPicks[i].name), key = "key|" + name + "|" + std::to_string(w) + "|" + std::to_string(used && !here);
      auto &layer = rowCache[key];
      if (layer.key.empty()) {
        beginLayer(layer, key, w, h);
        // As big as the key allows: one letter large, long names smaller.
        int glyphs = 0;
        for (unsigned char c : name)
          glyphs += (c & 0xC0) != 0x80;
        int size = glyphs > 1 ? 18 : 22;
        while (size > 11 && measure(name, size) > w - 8)
          --size;
        label(w / 2, (h - size - 4) / 2, name, size, used && !here ? faint : white, 255, false, 0, w - 6);
        endLayer();
      }
      place(layer.image, x, ky);
    }
    // The keys fill the screen down to the hint bar, so the help sits in
    // the header, right of the title.
    if (!notes.empty()) {
      auto &layer = rowCache["picknote|" + notes[0]];
      if (layer.key.empty()) {
        beginLayer(layer, "picknote|" + notes[0], 520, 44);
        auto lines = wrap(notes[0], 14, 520);
        for (size_t i = 0; i < lines.size() && i < 2; ++i)
          label(520, int(i) * 19, lines[i], 14, muted, 255, false, 1);
        endLayer();
      }
      place(layer.image, 944 - 520, 76);
      notes.clear();
    }
    y = hintsY;
  } else if (special == ControlsMap) {
    // The console, each button's mapping in a label beside it with a line to
    // the button, the screen as the touch screen's button, and pills below.
    const Image &art = icon("vita-640");
    const double k = UI::mapArtW / 1800.0;
    int sx, sy, sw, sh;
    UI::controlsRect(14, sx, sy, sw, sh);
    bool screenHere = u.setting == 14;
    if (screenHere)
      solidQuad(ruby, sx - 3, sy - 3, sw + 6, sh + 6, 1, 6);
    solidQuad({10, 10, 12}, sx, sy, sw, sh, 1, 2);
    if (!art.empty()) {
      Quad q;
      q.image = &art;
      q.x = float(UI::mapArtX), q.y = float(UI::mapArtY), q.w = float(UI::mapArtW);
      q.h = float(art.height * double(UI::mapArtW) / art.width);
      scene.push_back(q);
    }
    if (screenHere)
      solidQuad(ruby, sx, sy, sw, sh, 0.18, 2);
    // The focused shoulder button lights in its own shape.
    if ((u.setting == 11 || u.setting == 12) && !art.empty()) {
      int side = u.setting == 12, gx, gy;
      const Image &g = shoulder(side, gx, gy);
      if (!g.empty()) {
        const double f = double(UI::mapArtW) / art.width;
        Quad q;
        q.image = &g;
        q.x = float(UI::mapArtX + gx * f), q.y = float(UI::mapArtY + gy * f);
        q.w = float(g.width * f), q.h = float(g.height * f), q.alpha = 0.9f;
        scene.push_back(q);
      }
    }
    {
      std::string key = "mapscreen|" + u.controlValue(14) + "|" + languageCode(language());
      auto &l = rowCache[key];
      if (l.key.empty()) {
        beginLayer(l, key, sw - 16, 64);
        label((sw - 16) / 2, 0, tr("Front touch screen"), 12, faint, 255, false, 0, sw - 20);
        auto lines = wrap(u.controlValue(14), 15, sw - 24);
        for (size_t i = 0; i < lines.size() && i < 2; ++i)
          label((sw - 16) / 2, 20 + int(i) * 19, lines[i], 15, white, 255, false, 0, sw - 20);
        endLayer();
      }
      place(l.image, sx + 8, sy + sh / 2 - 30);
    }
    (void)k;
    // Lines first, so the labels sit on top of them. Rows 1-12 are the
    // buttons, 19 the right stick.
    static const int mapped[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 19};
    for (int row : mapped) {
      int x, y, w, h, ax, ay;
      UI::controlsRect(row, x, y, w, h);
      UI::controlsAnchor(row, ax, ay);
      bool here = u.setting == row;
      Color c = here ? ruby : white;
      double a = here ? 1 : 0.42;
      int ly = y + h / 2, from = x + w / 2 < 480 ? x + w : x;
      int x0 = std::min(from, ax), x1 = std::max(from, ax);
      solidQuad({0, 0, 0}, x0 - 1, ly - 2, x1 - x0 + 2, 4, 0.45);
      solidQuad(c, x0, ly - 1, x1 - x0, 2, a);
      int y0 = std::min(ly, ay), y1 = std::max(ly, ay);
      if (y1 > y0) {
        solidQuad({0, 0, 0}, ax - 2, y0 - 1, 4, y1 - y0 + 2, 0.45);
        solidQuad(c, ax - 1, y0, 2, y1 - y0, a);
      }
      solidQuad(c, ax - 4, ay - 4, 8, 8, here ? 1 : 0.8, 4);
    }
    for (int row : mapped) {
      int x, y, w, h;
      UI::controlsRect(row, x, y, w, h);
      bool here = u.setting == row;
      solidQuad(here ? ruby : Color{24, 24, 28}, x, y, w, h, here ? 1 : 0.92, 8);
      if (!here)
        solidQuad(white, x, y, w, h, 0.06, 8);
      std::string text = std::string(row == 19 ? "<rs>" : buttonIcon(row - 1)) + "  " + u.controlValue(row);
      std::string key = "mapbutton|" + text + "|" + std::to_string(w);
      auto &l = rowCache[key];
      if (l.key.empty()) {
        beginLayer(l, key, w - 16, h);
        label(0, (h - 20) / 2, text, 15, white, 255, false, -1, w - 18);
        endLayer();
      }
      place(l.image, x + 10, y);
    }
    // The other rows: switches and pages, as pills.
    for (int row : {0, 13, 15, 16, 17, 18}) {
      int x, y, w, h;
      UI::controlsRect(row, x, y, w, h);
      bool here = u.setting == row;
      if (overlay && !here)
        solidQuad({0, 0, 0}, x, y, w, h, 0.45, 10); // readable over the game
      solidQuad(here ? ruby : white, x, y, w, h, here ? 1 : 0.1, 10);
      static const char *names[] = {"Custom controls", "", "", "", "", "", "", "", "", "", "", "", "",
                                    "Tap to walk", "", "Rear touch pad", "Sticks and pointer", "Test inputs",
                                    "Reset everything"};
      std::string value = row == 0 ? tr(u.controls.enabled ? "On" : "Off") : row == 13 ? tr(u.controls.tapToWalk ? "On" : "Off")
                          : row == 15 ? u.controlValue(15) : row == 18 ? tr("Back to defaults") : tr("Open");
      std::string key = std::string("mappill|") + names[row] + "|" + value + "|" + languageCode(language());
      auto &l = rowCache[key];
      if (l.key.empty()) {
        beginLayer(l, key, w - 12, h);
        label(0, 6, tr(names[row]), 12, {214, 214, 218}, 255, false, -1, w - 14);
        label(0, 24, value, 14, white, 255, true, -1, w - 14);
        endLayer();
      }
      place(l.image, x + 8, y);
    }
    // What the focus is on, or the last message, in words.
    std::string line = !u.message.empty() ? u.message : u.controlLabel(u.setting) + ":  " + u.controlValue(u.setting);
    auto &l = rowCache["mapline|" + line];
    if (l.key.empty()) {
      // Between the label columns, under the drawing.
      beginLayer(l, "mapline|" + line, 520, 22);
      label(260, 0, line, 14, muted, 255, false, 0, 516);
      endLayer();
    }
    place(l.image, 220, 413);
    y = 600; // no notes
  } else if (special == ControlsPad) {
    bool rear = u.controlsPad == 2;
    int x, py, w, h;
    u.padRowRect(0, x, py, w, h);
    bool modeFocused = u.setting == 0;
    solidQuad(white, x, py, w, h, modeFocused ? 0.16 : 0.06, 8);
    if (modeFocused)
      solidQuad(ruby, x, py, 4, h, 1, 2);
    std::string key = "padmode|" + u.controlValue(0) + "|" + std::to_string(w) + "|" + std::to_string(h);
    auto &mode = rowCache[key];
    if (mode.key.empty()) {
      beginLayer(mode, key, w, h);
      label(18, (h - 22) / 2, tr("Mode"), 18, white);
      label(w - 16, (h - 22) / 2, "\xe2\x80\xb9  " + u.controlValue(0) + "  \xe2\x80\xba", 17, white, 255, false, 1);
      endLayer();
    }
    place(mode.image, x, py);
    // The console as the player holds it, front or back, with each zone
    // where it really is: Tokyoship's illustration (CC BY 3.0, see
    // licenses/Vita-illustration.txt), or its outline for the back.
    int sx, sy, sw, sh;
    UI::padSurface(sx, sy, sw, sh, rear);
    const Image &front = icon("vita-640");
    const Image &back = icon("vita-rear-640");
    if (rear && back.empty() && vitaBack.empty() && !front.empty()) {
      // The back: the same outline in the console's colour.
      vitaBack = front;
      for (size_t i = 0; i < vitaBack.rgba.size(); i += 4) {
        unsigned a = vitaBack.rgba[i + 3];
        vitaBack.rgba[i] = uint8_t(38 * a / 255);
        vitaBack.rgba[i + 1] = uint8_t(38 * a / 255);
        vitaBack.rgba[i + 2] = uint8_t(42 * a / 255);
      }
      vitaBack.touch();
    }
    // The back: the project's picture; its outline when that is missing.
    const Image &art = rear ? (back.empty() ? vitaBack : back) : front;
    if (rear && back.empty())
      UI::padSurface(sx, sy, sw, sh); // the outline has no pad drawn: use the screen's place
    double artH = art.empty() ? 0 : art.height * double(UI::padArtW) / art.width;
    if (!rear)
      solidQuad({10, 10, 12}, sx, sy, sw, sh, 1, 2);
    if (!art.empty()) {
      Quad q;
      q.image = &art;
      q.x = float(UI::padArtX), q.y = float(UI::padArtY), q.w = float(UI::padArtW), q.h = float(artH);
      scene.push_back(q);
    }
    if (rear && back.empty())
      solidQuad({70, 70, 76}, sx, sy, sw, sh, 1, 12); // the touch pad
    else if (rear)
      solidQuad(white, sx, sy, sw, sh, 0.06, 6); // the pad, lightly marked
    int padMode = rear ? u.controls.rear : u.controls.front;
    bool drawn = !rear && (padMode == 1 || padMode == 3);
    std::string surfaceKey = std::string("padsurface|") + (rear ? "rear|" : "front|") + std::to_string(drawn) + "|" +
                             languageCode(language());
    auto &caption = rowCache[surfaceKey];
    if (caption.key.empty()) {
      beginLayer(caption, surfaceKey, 640, 18);
      label(0, 0,
            tr(rear    ? "The back of the console, left and right as you hold it"
               : drawn ? "Drawn on the game picture, which is a different size in each game"
                       : "The whole screen, black borders included"),
            12, faint);
      endLayer();
    }
    place(caption.image, UI::padArtX, UI::padArtY - 19);
    if (drawn) {
      // The example game picture the drawn buttons sit in.
      int gx, gy, gw, gh;
      UI::padGamePicture(gx, gy, gw, gh);
      solidQuad(white, gx, gy, gw, gh, 0.08, 0);
      if (padMode == 3)
        solidQuad(white, gx, gy + gh * .55, gw * .36, gh * .45, 0.22, 6); // the D-pad
    }
    if (u.controlsCount() == 1) {
      std::string text = tr(padMode == 2 ? (rear ? "Trackpad: hold <l> and slide to move the pointer." : "Mouse pointer: touches move and click the pointer.")
                                      : "Off. Choose a mode with Left and Right to map zones.");
      auto &l = rowCache["padoff|" + text];
      if (l.key.empty()) {
        beginLayer(l, "padoff|" + text, sw - 20, 40);
        auto lines = wrap(text, 14, sw - 20);
        for (size_t i = 0; i < lines.size() && i < 2; ++i)
          label((sw - 20) / 2, int(i) * 19, lines[i], 14, muted, 255, false, 0);
        endLayer();
      }
      place(l.image, sx + 10, sy + sh / 2 - 18);
    }
    for (int r = 1; r < u.controlsCount(); ++r) {
      u.padRowRect(r, x, py, w, h);
      bool here = u.setting == r;
      solidQuad(here ? ruby : white, x, py, w, h, here ? 0.9 : 0.18, 6);
      if (!here)
        solidQuad({0, 0, 0}, x + 1, py + 1, w - 2, h - 2, 0.25, 5);
      std::string zkey = "zone|" + u.controlValue(r) + "|" + std::to_string(w) + "x" + std::to_string(h);
      auto &zone = rowCache[zkey];
      if (zone.key.empty()) {
        beginLayer(zone, zkey, w, h);
        // Small zones show only what they do.
        int size = h < 50 || w < 90 ? 12 : 16;
        auto lines = wrap(u.controlValue(r), size, w - 8);
        int lineH = size + 3, top = (h - int(std::min<size_t>(lines.size(), 2)) * lineH) / 2;
        for (size_t i = 0; i < lines.size() && i < 2; ++i)
          label(w / 2, top + int(i) * lineH, lines[i], size, white, 255, size > 12, 0, w - 6);
        endLayer();
      }
      place(zone.image, x, py);
    }
    // Which zone the focus is on, in words, under the picture.
    if (u.setting >= 1) {
      std::string text = u.zoneName(u.setting) + ":  " + u.controlValue(u.setting);
      auto &l = rowCache["padfocus|" + text];
      if (l.key.empty()) {
        beginLayer(l, "padfocus|" + text, 560, 22);
        label(280, 0, text, 15, white, 255, false, 0, 556);
        endLayer();
      }
      place(l.image, UI::padArtX, UI::padArtY + artH + 2);
    }
    y = 474;
  } else if (special == ControlsTest) {
    // A drawing of the Vita: each button lights up ruby while held, the
    // sticks' dots follow the sticks, and fingers show on the screen and on
    // the back pad. Only quads move, so nothing is redrawn as text.
    auto &smp = u.controlsSample;
    auto held = [&](int bit) { return (smp.buttons & (1u << bit)) != 0; };
    enum { Cross, Circle, Square, Triangle, Select, Start, Up, Down, Left, Right, L, R };
    auto lit = [&](int bit, double x, double y, double w, double h, double radius) {
      solidQuad(held(bit) ? ruby : white, x, y, w, h, held(bit) ? 1 : 0.16, radius);
    };
    // The console: Tokyoship's illustration (CC BY 3.0, see
    // licenses/Vita-illustration.txt), 1800 px wide originally, drawn 640 wide.
    const Image &vita = icon("vita-640");
    const double ox = 40, oy = 154, k = 640.0 / 1800;
    auto at = [&](double x, double y) { return std::make_pair(ox + x * k, oy + y * k); };
    // The screen is cut out of the picture: fill it, with a dot per finger.
    auto s0 = at(356, 66), s1 = at(1443, 673);
    const double sx = s0.first, sy = s0.second, sw = s1.first - s0.first, sh = s1.second - s0.second;
    solidQuad({10, 10, 12}, sx, sy, sw, sh, 1, 2);
    place(vita, ox, oy);
    for (auto &c : smp.front)
      solidQuad(ruby, sx + std::max(0.f, std::min(1.f, c.x)) * (sw - 16), sy + std::max(0.f, std::min(1.f, c.y)) * (sh - 16), 16, 16, 1, 8);
    // A held button glows ruby over the drawing: a soft halo and a core that
    // leaves its symbol readable.
    auto glow = [&](int bit, double x, double y, double w, double h) {
      if (!held(bit))
        return;
      auto p = at(x, y);
      double gw = w * k, gh = h * k, r = std::min(gw, gh) / 2;
      solidQuad(ruby, p.first - gw / 2 - 6, p.second - gh / 2 - 6, gw + 12, gh + 12, 0.35, r + 6);
      solidQuad(ruby, p.first - gw / 2, p.second - gh / 2, gw, gh, 0.6, r);
    };
    glow(Triangle, 1627, 197, 72, 72);
    glow(Square, 1547, 273, 72, 72);
    glow(Circle, 1707, 273, 72, 72);
    glow(Cross, 1627, 350, 72, 72);
    glow(Up, 171, 208, 64, 74);
    glow(Down, 171, 342, 64, 74);
    glow(Left, 104, 274, 74, 64);
    glow(Right, 238, 274, 74, 64);
    glow(Select, 1550, 644, 84, 40);
    glow(Start, 1648, 644, 84, 40);
    // The shoulders light their own shape, cut out of the drawing.
    for (int side = 0; side < 2; ++side)
      if (held(side ? R : L)) {
        int gx, gy;
        const Image &g = shoulder(side, gx, gy);
        if (!g.empty()) {
          const double f = 640.0 / vita.width;
          Quad q;
          q.image = &g;
          q.x = float(ox + gx * f), q.y = float(oy + gy * f), q.w = float(g.width * f), q.h = float(g.height * f);
          q.alpha = 0.85f;
          scene.push_back(q);
        }
      }
    // Sticks: a dot that follows each stick, ruby once it leaves the centre.
    auto stick = [&](double x, double y, int vx, int vy) {
      auto p = at(x + (vx - 128) / 128.0 * 34, y + (vy - 128) / 128.0 * 34);
      bool moved = std::abs(vx - 128) > 24 || std::abs(vy - 128) > 24;
      solidQuad(moved ? ruby : white, p.first - 8, p.second - 8, 16, 16, moved ? 1 : 0.45, 8);
    };
    stick(222, 487, smp.lx, smp.ly);
    stick(1580, 487, smp.rx, smp.ry);
    // The back touch pad beside the console.
    const double px = 712, py = 186, pw = 218, ph = 118;
    solidQuad(white, px, py, pw, ph, 0.07, 10);
    for (auto &c : smp.rear)
      solidQuad(ruby, px + std::max(0.f, std::min(1.f, c.x)) * (pw - 14), py + std::max(0.f, std::min(1.f, c.y)) * (ph - 14), 14, 14, 1, 7);
    const double bh = 289, by = oy;
    // Labels, and what the held buttons do in this game; redrawn only when
    // the held buttons change.
    std::string pressed;
    for (int i = 0; i < 12; ++i)
      if (held(i))
        pressed += buttonText(i) + "  \xe2\x86\x92  " + controlText(u.controls.buttons[i]) + "\n";
    std::string actions;
    for (auto &a : controlActions)
      if (a.code && u.controlsResult.actions[a.code])
        actions += std::string(tr(a.name)) + "  ";
    for (auto &k : controlPicks)
      if (controlIsKey(k.value) && u.controlsResult.keys[k.value - controlKeyBase])
        actions += trf("Key {}", {k.name}) + "  ";
    std::string key = "vita|" + pressed + "|" + actions + "|" + std::to_string(u.controls.enabled) + "|" + languageCode(language());
    if (beginLayer(bodyLayer, key, 220, 270)) {
      label(0, 0, tr("Back touch pad"), 13, faint);
      label(0, 140, tr("Held"), 13, faint);
      int ly = 160;
      if (pressed.empty())
        label(0, ly, tr("Press any button"), 15, muted);
      for (size_t at = 0, next; at < pressed.size() && ly < 230; at = next + 1, ly += 20) {
        next = pressed.find('\n', at);
        label(0, ly, pressed.substr(at, next - at), 14, white, 255, false, -1, 210);
      }
      endLayer();
    }
    place(bodyLayer.image, px, py - 20);
    std::string screenKey = "vitascreen|" + std::to_string(smp.front.empty()) + "|" + languageCode(language());
    auto &screenText = rowCache[screenKey];
    if (screenText.key.empty()) {
      beginLayer(screenText, screenKey, int(sw), 20);
      if (smp.front.empty())
        label(int(sw) / 2, 0, tr("Touch the screen"), 14, faint, 255, false, 0);
      endLayer();
    }
    place(screenText.image, sx, sy + sh / 2 - 10);
    notes = {actions.empty() ? tr(u.controls.enabled ? "Nothing this game's profile does is held." : "Custom controls are off: this shows the raw buttons.")
                             : trf("In game this does: {}", {actions})};
    y = by + bh + 14;
  }

  // Notes under everything else, wrapped to the column.
  if (!notes.empty()) {
    // The room left under the rows; long notes (or a long language) get a
    // smaller size before they would cover a row.
    int room = hintsY - 6 - (y + 2);
    std::string key = std::to_string(room) + "\n";
    for (auto &n : notes)
      key += n + "\n";
    if (notesLayer.key != key) {
      int size = 15, lineH = 21, gap = 8;
      std::vector<std::vector<std::string>> paragraphs;
      auto layout = [&] {
        paragraphs.clear();
        int h = -gap;
        for (auto &n : notes) {
          paragraphs.push_back(wrap(n, size, G.w));
          h += int(paragraphs.back().size()) * lineH + gap;
        }
        return std::max(1, h);
      };
      int h = layout();
      if (h > room) {
        size = 13, lineH = 17, gap = 6;
        h = layout();
      }
      beginLayer(notesLayer, key, G.w, h);
      int ly = 0;
      for (auto &p : paragraphs) {
        for (auto &line : p) {
          label(0, ly, line, size, muted);
          ly += lineH;
        }
        ly += gap;
      }
      endLayer();
    }
    // Notes never run into the hint bar: they move up if they must.
    int top = std::min(y + 2, hintsY - 6 - notesLayer.image.height);
    place(notesLayer.image, G.x, std::max(G.top, top));
  }

  // The bottom band, hints, and messages.
  {
    Quad b;
    b.image = &band;
    b.y = float(hintsY - 4), b.w = float(width), b.h = float(height - (hintsY - 4));
    scene.push_back(b);
  }
  std::string counter;
  if (!rows.empty() && focusRow >= 0 && int(rows.size()) > shown)
    counter = std::to_string(focusRow + 1) + "|" + std::to_string(rows.size());
  hintBar(u, left, right, counter);
  // The cheats number editor: a panel over the page with one box per digit,
  // the digit Up and Down change lit, with arrows over and under it.
  if (u.page == Page::Cheats && u.cheatEditRow >= 0 && size_t(u.cheatEditRow) < u.cheatRows.size()) {
    auto &r = u.cheatRows[size_t(u.cheatEditRow)];
    const int digits = u.cheatEditDigits();
    const long long v = u.cheatEditValue < 0 ? -u.cheatEditValue : u.cheatEditValue;
    std::string text = std::to_string(v);
    const int used = int(text.size()); // digits that are not leading zeros
    if (int(text.size()) < digits)
      text.insert(0, size_t(digits - int(text.size())), '0');
    const int box = 46, gap = 6, comma = 14, sign = r.lo < 0 ? box + gap : 0;
    int total = sign + digits * box + (digits - 1) * gap + ((digits - 1) / 3) * comma;
    const int pw = std::max(560, total + 80), ph = 262, px = 480 - pw / 2, py = 132;
    solidQuad({0, 0, 0}, 0, 0, width, hintsY - 4, 0.55);
    solidQuad({24, 24, 27}, px, py, pw, ph, 0.97, 18);
    std::string key = "cheatedit|" + r.label + "|" + std::to_string(u.cheatEditValue) + "|" + std::to_string(u.cheatEditDigit) +
                      "|" + std::to_string(digits) + "|" + std::to_string(r.lo) + "|" + std::to_string(r.hi);
    auto &layer = cheatEditLayer;
    if (beginLayer(layer, key, pw, ph)) {
      label(pw / 2, 18, r.raw ? r.label : tr(r.label), 22, white, 255, false, 0, pw - 40);
      int x = (pw - total) / 2;
      const int by = 92;
      if (r.lo < 0) {
        rect(x, by, box, 60, {255, 255, 255}, 26, 8);
        label(x + box / 2, by + 12, u.cheatEditValue < 0 ? "-" : "+", 30, white, 255, false, 0);
        x += box + gap;
      }
      for (int i = 0; i < digits; ++i) {
        const int place = digits - 1 - i; // 0: ones
        const bool lit = place == u.cheatEditDigit, lead = place >= used && place > 0;
        if (lit) {
          rect(x, by, box, 60, ruby, 255, 8);
          label(x + box / 2, by - 30, "\xe2\x86\x91", 20, white, 255, false, 0);
          label(x + box / 2, by + 66, "\xe2\x86\x93", 20, white, 255, false, 0);
        } else
          rect(x, by, box, 60, {255, 255, 255}, 26, 8);
        label(x + box / 2, by + 12, std::string(1, text[size_t(i)]), 30, lead && !lit ? faint : white, 255, false, 0);
        x += box + gap;
        if (place > 0 && place % 3 == 0) {
          label(x + comma / 2 - gap / 2, by + 26, ",", 26, muted, 255, false, 0);
          x += comma;
        }
      }
      label(pw / 2, 186, trf("From {} to {}", {groupDigits(r.lo), groupDigits(r.hi)}), 15, muted, 255, false, 0, pw - 40);
      label(pw / 2, 212, tr("Nothing changes until you press Set."), 15, muted, 255, false, 0, pw - 40);
      endLayer();
    }
    place(layer.image, px, py);
  }
  auto message = !u.message.empty() ? u.message : u.browser ? u.browser->error : std::string();
  if (!message.empty() && special != ControlsPad && u.page != Page::Controls) {
    if (beginLayer(messageLayer, message, width, 40)) {
      int w = std::min(880, measure(message, 15) + 34);
      rect(480 - w / 2, 0, w, 36, {22, 22, 24}, 245, 18);
      label(480, 9, message, 15, {230, 230, 232}, 255, false, 0, 860);
      endLayer();
    }
    place(messageLayer.image, 0, hintsY - 46);
  }
  if (!u.launchingName.empty()) {
    solidQuad({0, 0, 0}, 0, 0, width, height, 0.85);
    if (beginLayer(messageLayer, "launch|" + u.launchingName + "|" + u.preparationLabel, width, 90)) {
      label(480, 0, trf("Starting {}", {u.launchingName}), 28, white, 255, true, 0, 900);
      label(480, 44, u.preparationLabel.empty() ? tr("Loading") : u.preparationLabel, 16, muted, 255, false, 0, 900);
      endLayer();
    }
    place(messageLayer.image, 0, 232);
    if (u.preparationFraction >= 0) {
      solidQuad(white, 280, 318, 400, 6, 0.14, 3);
      solidQuad(ruby, 280, 318, 400 * std::min(1.0, u.preparationFraction), 6, 1, 3);
    }
  }
}
} // namespace hardrpg
