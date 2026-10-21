// Every screen other than home (settings, game settings, controls, the
// folder browser, About, reports and questions) as a "sheet": the home
// screen's top bar and hint bar around a header, a list of rows and notes.
//
// Rows are cached text layers; the focus is a GPU quad behind them, so moving
// through a list redraws no text. Row positions come from home_layout.h,
// which touch (UI::sheetTouch) uses too.
#include "home_layout.h"
#include "release.h"
#include "util/json5pp.hpp"
#include "view.h"
#include <algorithm>
#include <cctype>
#include <cmath>

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
  static const char *names[] = {"Unknown engine", "RPG Maker XP", "RPG Maker VX", "RPG Maker VX Ace", "RPG Maker 2000 / 2003"};
  return version >= 0 && version <= 4 ? names[version] : names[0];
}
std::string upper(std::string s) {
  for (auto &c : s)
    c = char(std::toupper(static_cast<unsigned char>(c)));
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
    auto flush = [&] {
      if (line.empty())
        line = word;
      else if (measure(line + " " + word, size, alt) <= maxWidth)
        line += " " + word;
      else {
        lines.push_back(line);
        line = word;
      }
      word.clear();
    };
    for (char c : para) {
      if (c == ' ') {
        if (!word.empty())
          flush();
      } else
        word += c;
    }
    if (!word.empty())
      flush();
    lines.push_back(line);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return lines;
}

void View::buildSheet(const UI &u) {
  scene.clear();
  ensureStrips();
  pendingLoads = false;

  // What the screen shows, filled in by state below, then drawn.
  struct Row {
    enum Kind { Nav, Choice, Toggle, Action, Primary, Danger, Info, Folder, Zip, Plain } kind = Nav;
    std::string label, value, detail;
    bool on = false;
  };
  std::string section, title, subtitle;
  std::vector<Row> rows;
  int focusRow = -1, first = 0, shown = sheetRows, rowH = sheetRowH;
  std::vector<std::string> notes, body;
  int bodyFirst = 0, bodyLines = 12;
  std::vector<Hint> left, right;
  int topFocus = -1;
  enum Special { NoSpecial, About, ControlsTest, ControlsPick, ControlsPad } special = NoSpecial;
  auto row = [&](Row::Kind kind, const std::string &label, const std::string &value = "", bool on = false) {
    Row r;
    r.kind = kind;
    r.label = label;
    r.value = value;
    r.on = on;
    rows.push_back(r);
  };
  const Hint cross{"btn-cross-26", "Select", Button::Cross}, back{"btn-circle-26", "Back", Button::Circle};
  auto hint = [](const char *glyph, const std::string &text, Button b) { return Hint{glyph, text, b}; };
  auto values = [&]() -> const Json::object_type & { return u.displayValues.as_object(); };
  auto boolean = [&](const char *name, bool fallback) {
    auto it = values().find(name);
    return it != values().end() && it->second.is_boolean() ? it->second.as_boolean() : fallback;
  };

  // Display, metrics and RTP pages are shared by settings and game settings.
  auto displayPage = [&] {
    title = "Display";
    subtitle = u.gameOptions ? u.gameEntry.name : "Default for every game";
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
    focusRow = u.setting;
    notes = {u.gameOptions ? "Saved for this game. Square goes back to the default for every game."
                           : "Used by games without settings of their own."};
    left = {hint("btn-cross-26", "Change", Button::Cross), hint("btn-square-26", u.gameOptions ? "Use default" : "Reset", Button::Square), back};
  };
  auto metricsPage = [&] {
    title = "Performance and speed";
    subtitle = u.gameOptions ? u.gameEntry.name : "Default for every game";
    int mode = 0;
    auto i = values().find("performanceMetrics");
    if (i != values().end() && i->second.is_integer() && i->second.as_integer() >= 0 && i->second.as_integer() <= 2)
      mode = i->second.as_integer();
    const char *modes[] = {"Off", "FPS", "FPS and frame timing"};
    row(Row::Choice, "Overlay in game", modes[mode]);
    row(Row::Toggle, "Fast forward with L+R", "", boolean("fastForward", true));
    focusRow = u.setting;
    if (u.setting == 1)
      notes = {"L+R switches up to twice the speed; \xc2\xbb\xc2\xbb shows in the corner while it is on. Busy maps run as fast "
               "as they can.",
               "Any button can also be mapped to Fast forward in Controls. Applies the next time a game starts."};
    else
      notes = {"FPS: frames shown each second. MS: average time between frames, waits included. Peak: the longest frame "
               "in about the last five seconds.",
               "Drawn in an empty border when there is one, otherwise in a corner. Applies the next time a game starts."};
    left = {hint("btn-cross-26", "Change", Button::Cross), hint("btn-square-26", u.gameOptions ? "Use default" : "Off", Button::Square), back};
  };
  auto rtpPage = [&] {
    title = "RTP locations";
    subtitle = "Shared by every game";
    rowH = rtpRowH;
    shown = rtpPackCount;
    for (int i = 0; i < rtpPackCount; ++i) {
      auto entry = u.rtpChoices.as_object().find(packs[i]);
      Row r;
      r.kind = Row::Nav;
      r.label = packLabel(packs[i]) + " RTP";
      r.value = i < int(u.rtpStatuses.size()) ? u.rtpStatuses[i] : "";
      r.detail = entry == u.rtpChoices.as_object().end() ? u.model.paths.at(std::string("rtp/") + packs[i]) + " (automatic)"
                                                         : entry->second.as_string();
      r.detail = tail(r.detail, 84);
      rows.push_back(r);
    }
    focusRow = u.rtpSelected;
    notes = {"An RTP in a ZIP is unpacked once; the original pack is kept."};
    left = {hint("btn-cross-26", "Choose", Button::Cross), hint("btn-square-26", "Automatic", Button::Square),
            hint("btn-triangle-26", "Check again", Button::Triangle), back};
  };

  if (!u.releaseReport.empty()) {
    section = "Update";
    title = "HardRPG " + u.releaseNotice.tag;
    subtitle = "A newer release is out";
    body = u.releaseReport;
    bodyFirst = u.releaseScroll;
    bodyLines = 11;
    left = {hint("btn-cross-26", "Continue", Button::Cross)};
    right = {{"UP DOWN", "Scroll", Button::None}};
  } else if (u.page == Page::Controls) {
    section = "Game settings";
    subtitle = u.controlsEntry.name;
    if (u.controlsTesting) {
      special = ControlsTest;
      title = "Test controls";
      subtitle = "Press buttons, move the sticks, touch the screen or the back";
      left = {hint("btn-circle-26", "Stop testing", Button::Circle)};
    } else if (u.controlPicking) {
      special = ControlsPick;
      title = u.controlLabel(u.setting);
      subtitle = "now does: " + u.controlValue(u.setting);
      notes = {"Top row: RPG Maker actions (X=A: RPG Maker's X, key A on PC). Below: keys. Dimmed: in use."};
      left = {hint("btn-cross-26", "Assign", Button::Cross), hint("btn-circle-26", "Cancel", Button::Circle)};
      right = {{nullptr, "Tap twice to assign", Button::None}};
    } else if (u.controlsPad) {
      special = ControlsPad;
      bool rear = u.controlsPad == 2;
      title = rear ? "Rear touch pad" : "Front touch screen";
      int mode = rear ? u.controls.rear : u.controls.front;
      notes = {!u.message.empty() ? u.message
               : rear ? (mode == 1 ? "Two zones along the top edge, clear of your grip."
                                   : "Fingers already on the pad when the game starts are ignored.")
               : mode == 1 || mode == 3 ? "These buttons are drawn on screen. Whole screen, halves and corners are invisible."
               : mode >= 4 ? "Invisible zones: nothing is drawn. Several fingers press several zones."
                           : "Whole screen, halves and corners are invisible touch zones."};
      left = {hint("btn-cross-26", "Map zone", Button::Cross), back};
      right = {{nullptr, "Left and right: mode or zone", Button::None}};
    } else {
      title = u.controlsAdvanced ? "Sticks and pointer" : "Controls";
      for (int i = 0; i < u.controlsCount(); ++i)
        row(i >= 1 && i <= 12 && !u.controlsAdvanced ? Row::Nav : Row::Choice, u.controlLabel(i), u.controlValue(i));
      focusRow = u.setting;
      first = u.settingFirst;
      notes = {!u.message.empty() ? u.message
               : u.controlsAdvanced ? "Pointer options need a game that reads the mouse."
               : u.setting >= 1 && u.setting <= 12 ? "Cross gives this button a keyboard key or an RPG Maker action."
               : u.controls.enabled ? "Each button does what is shown. Saved for this game."
                                    : "Pick a button and press Cross to map it to any keyboard key."};
      left = {cross, back};
    }
  } else if (!u.errorReport.empty()) {
    section = "Error report";
    title = "The game ended with an error";
    body = u.errorReport;
    bodyFirst = u.errorScroll;
    left = {hint("btn-cross-26", "Back", Button::Cross), hint("btn-square-26", "Save report", Button::Square)};
    right = {{"UP DOWN", "Scroll", Button::None}};
  } else if (!u.missingRtp.empty()) {
    section = "Can't start the game";
    title = "Missing " + packLabel(u.missingRtp) + " RTP";
    subtitle = "This game needs the RTP's shared graphics and sounds.";
    row(Row::Primary, "Choose an RTP folder or ZIP");
    row(Row::Plain, "Back to games");
    focusRow = 0;
    notes = {"Looked in: " + u.model.paths.at("rtp/" + u.missingRtp)};
    left = {hint("btn-cross-26", "Choose RTP", Button::Cross), back};
  } else if (u.browser) {
    bool rtp = u.browseMode == BrowseMode::Rtp, folder = u.browseMode == BrowseMode::Folder;
    section = rtp ? "RTP locations" : folder ? "Game folders" : "Library";
    title = rtp ? "Choose an RTP" : folder ? "Choose a game folder" : "Add games";
    subtitle = tail(u.browser->path.empty() ? "Choose a storage device" : u.browser->path, 70);
    rowH = browserRowH;
    shown = browserRows;
    for (auto &f : u.browser->list)
      row(f.directory ? Row::Folder : Row::Zip, f.name);
    focusRow = u.browserSelected;
    first = u.browserFirst;
    if (u.browser->list.empty())
      notes = {u.browser->path.empty() ? "No storage found."
               : rtp || folder        ? "No folders here. Square uses this folder."
                                      : "No folders here. Square looks for games in this folder."};
    else if (!rtp && !folder)
      notes = {"Square looks for games in the folder shown above, including the folders inside it. Games stay where they are."};
    left = {hint("btn-cross-26", "Open", Button::Cross),
            hint("btn-square-26", rtp || folder ? "Use this folder" : "Add games here", Button::Square), back};
    right = {{"START", "Close", Button::Start}};
  } else if (u.gameOptions) {
    section = "Game settings";
    topFocus = -1;
    if (u.page == Page::Game) {
      title = u.gameEntry.name;
      subtitle = engineLong(u.gameEntry.version);
      const char *names[] = {"Controls", "Display", "Audio and MIDI", "Performance and speed", "RTP locations",
                             "Rename", "Game information", "Play", "Remove from library", "Delete the ZIP, keep the game"};
      Row::Kind kinds[] = {Row::Nav, Row::Nav, Row::Nav, Row::Nav, Row::Nav, Row::Action, Row::Nav, Row::Primary, Row::Danger, Row::Danger};
      for (int i = 0; i < u.gameOptionsCount(); ++i)
        row(kinds[i], names[i]);
      focusRow = u.setting;
      first = u.settingFirst;
      left = {hint("btn-cross-26", "Open", Button::Cross), back};
    } else if (u.page == Page::Audio) {
      title = "Audio and MIDI";
      subtitle = u.gameEntry.name;
      const char *keys[] = {"enableAudio", "midiReverb", "midiChorus"};
      const char *names[] = {"Sound", "MIDI reverb", "MIDI chorus"};
      for (int i = 0; i < 3; ++i)
        row(Row::Toggle, names[i], "", boolean(keys[i], i == 0));
      focusRow = u.setting;
      notes = {"Saved for this game; applies the next time it starts.", "MIDI effects cost CPU time and are off by default."};
      left = {hint("btn-cross-26", "Change", Button::Cross), hint("btn-square-26", "Use default", Button::Square), back};
    } else if (u.page == Page::Remove) {
      auto q = u.removeQuestions[u.removeStep];
      bool source = !u.removeInfo.folder.empty() || !u.removeInfo.zip.empty();
      bool copy = q == UI::DeleteUnpacked || (!source && u.gameEntry.node.archive);
      bool zipOnly = q == UI::DeleteZipOnly;
      section = "Remove " + u.gameEntry.name;
      title = zipOnly ? "Delete the ZIP to free its space?" : copy ? "Delete its unpacked copy too?" : "Delete its game files too?";
      subtitle = zipOnly ? "The game stays listed and plays from its unpacked copy."
                 : u.removeStep == 0 ? "It will be removed from the library."
                                     : std::string("Game files: ") + (u.removeFiles ? "will be deleted." : "kept.");
      bool one = copy || zipOnly;
      row(Row::Plain, one ? "No, keep it" : "No, keep them");
      row(Row::Danger, one ? "Yes, delete it" : "Yes, delete them");
      focusRow = u.setting;
      notes = {tail(u.removePath(q), 90),
               zipOnly ? "Saves and settings stay: they are in the unpacked copy."
               : copy  ? "The unpacked copy holds the game's saves. This cannot be undone."
                       : "Saves kept inside the game files go with them. This cannot be undone."};
      left = {hint("btn-cross-26", "Answer", Button::Cross), hint("btn-circle-26", "Cancel", Button::Circle)};
    } else if (u.page == Page::GameInfo) {
      title = "Game information";
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
            notes.push_back("Details from " + text("sources") + ".");
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
    right = {{"UP DOWN", "Release notes", Button::None}};
  } else {
    section = "Settings";
    topFocus = 1;
    if (u.page == Page::Updates) {
      title = "Release checks";
      row(Row::Toggle, "Check for new releases at startup", "", u.automaticUpdates);
      row(Row::Action, "Check now");
      row(Row::Nav, "Newer release notes", u.releaseNotice.tag.empty() ? "None found" : u.releaseNotice.tag);
      focusRow = u.setting;
      notes = {"Checks published Alpha releases. Nothing is shown when you are up to date or offline.",
               "Updates are downloaded and installed by hand."};
      left = {cross, back};
    } else if (u.page == Page::Display)
      displayPage();
    else if (u.page == Page::Metrics)
      metricsPage();
    else if (u.page == Page::Rtp)
      rtpPage();
    else if (u.page == Page::Folders) {
      title = "Game folders";
      subtitle = "Default: " + tail(u.model.paths.at("games"), 48);
      row(Row::Action, "Add another folder");
      for (auto &f : u.gameFolders)
        row(Row::Folder, tail(f, 70));
      focusRow = u.setting;
      first = u.settingFirst;
      notes = {"Folders are searched for games each time HardRPG starts. Removing a folder here keeps its games and saves."};
      left = {hint("btn-cross-26", "Browse", Button::Cross), hint("btn-square-26", "Remove", Button::Square), back};
    } else if (u.page == Page::Keys) {
      title = "Game info and artwork";
      subtitle = "Covers, backgrounds, logos and details such as the year, developer and description";
      // A key shows only its ends, so it is recognisable but not readable over a shoulder.
      auto masked = [](const std::string &key) {
        return key.empty() ? std::string("Add a key") : key.size() <= 8 ? std::string("Saved") : key.substr(0, 4) + "\xe2\x80\xa6" + key.substr(key.size() - 4);
      };
      row(Row::Toggle, "Blur adult artwork", "", u.blurAdult);
      row(Row::Nav, "SteamGridDB", masked(u.readKey(0)));
      row(Row::Nav, "RAWG", masked(u.readKey(1)));
      row(Row::Info, "Steam", "No key needed");
      row(Row::Info, "VNDB", "No key needed");
      focusRow = u.setting;
      if (u.setting == 0)
        notes = {"Covers and backgrounds a source marks as adult are shown blurred in the library and the artwork chooser."};
      else
        notes = {"Sources are asked in this order. Steam and VNDB need no key.",
                 "SteamGridDB: covers, backgrounds and logos for almost any game. Free key: steamgriddb.com, Preferences, API.",
                 "RAWG: release dates, developers, publishers and genres. Free key: rawg.io/apidocs."};
      left = {u.setting == 0 ? hint("btn-cross-26", "Change", Button::Cross) : hint("btn-cross-26", "Type key", Button::Cross)};
      if (u.setting > 0)
        left.push_back(hint("btn-square-26", "Remove key", Button::Square));
      left.push_back(back);
    } else if (u.page == Page::Errors) {
      title = "Game error history";
      for (auto &record : u.errors)
        row(Row::Nav, record.game, record.timestamp);
      focusRow = u.setting;
      first = u.settingFirst;
      if (u.errors.empty())
        notes = {"No game has ended with an error."};
      left = {hint("btn-cross-26", "View", Button::Cross), hint("btn-square-26", "Save report", Button::Square), back};
    } else {
      title = "Settings";
      const char *metrics[] = {"Off", "FPS", "FPS and timing"};
      (void)metrics;
      row(Row::Nav, "Game display");
      row(Row::Nav, "RTP locations");
      row(Row::Nav, "Game folders", u.gameFolders.empty() ? "" : std::to_string(u.gameFolders.size()) + " added");
      row(Row::Nav, "Game error history", u.errors.empty() ? "" : std::to_string(u.errors.size()));
      row(Row::Nav, "Performance and speed");
      row(Row::Nav, "Release checks", u.automaticUpdates ? "At startup" : "Off");
      int keys = !u.readKey(0).empty() + !u.readKey(1).empty();
      row(Row::Nav, "Game info and artwork", keys ? std::to_string(2 + keys) + " sources" : "Steam, VNDB");
      row(Row::Action, "Exit HardRPG");
      focusRow = u.setting;
      first = u.settingFirst;
      left = {cross, back};
    }
  }

  // Background: the last game shown on home, far back behind a dark veil.
  {
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
    if (beginLayer(headerLayer, key, width - sheetX, 76)) {
      label(0, 0, upper(section), 12, faint);
      int tw = measure(title, 30, true);
      label(0, 16, title, 30, white, 255, true, -1, sheetW + 40);
      if (!subtitle.empty()) {
        int sw = measure(subtitle, 15);
        if (tw + 18 + sw <= sheetW + 40)
          label(tw + 18, 29, subtitle, 15, muted);
        else
          label(0, 54, subtitle, 15, muted, 255, false, -1, sheetW + 40);
      }
      endLayer();
    }
    place(headerLayer.image, sheetX, sheetTitleY);
  }

  // Rows: cached text over a quad; the focused one brighter, with the ruby mark.
  int rowsShown = std::min(int(rows.size()) - first, shown);
  int box = rowH - 6;
  for (int i = 0; i < rowsShown; ++i) {
    auto &r = rows[first + i];
    int y = sheetTop + i * rowH;
    bool focused = first + i == focusRow;
    if (r.kind == Row::Primary)
      solidQuad(ruby, sheetX, y, sheetW, box, focused ? 1 : 0.7, 8);
    else if (r.kind != Row::Info || focused)
      solidQuad(white, sheetX, y, sheetW, box, focused ? 0.16 : 0.06, 8);
    if (focused && r.kind != Row::Primary)
      solidQuad(ruby, sheetX, y, 4, box, 1, 2);
    std::string key = std::to_string(int(r.kind)) + "|" + r.label + "|" + r.value + "|" + r.detail + "|" + std::to_string(r.on) + "|" +
                      std::to_string(box);
    auto &layer = rowCache[key];
    if (layer.key.empty()) {
      beginLayer(layer, key, sheetW, box);
      int textX = 18;
      const char *glyphName = r.kind == Row::Folder ? "folder-26" : r.kind == Row::Zip ? "memcard-26" : r.kind == Row::Action && r.label.find("Add") == 0 ? "plus-31" : nullptr;
      if (glyphName) {
        const Image &g = icon(glyphName);
        blitScaled(g, 14, (box - 24) / 2, 24, 24);
        textX = 50;
      }
      int labelY = r.detail.empty() ? (box - 22) / 2 : 5;
      Color labelColor = r.kind == Row::Danger ? rubyHi : r.kind == Row::Info ? muted : white;
      int labelSize = r.kind == Row::Info ? 16 : 18;
      int right = sheetW - 16;
      if (r.kind == Row::Toggle) {
        // A switch: ruby and to the right when on.
        rect(right - 44, (box - 24) / 2, 44, 24, r.on ? ruby : Color{80, 80, 86}, 255, 12);
        rect(r.on ? right - 22 : right - 42, (box - 24) / 2 + 2, 20, 20, white, 255, 10);
        right -= 58;
      } else if (r.kind == Row::Choice) {
        label(right, (box - 22) / 2, "\xe2\x80\xba", 20, muted, 255, false, 1);
        int vw = measure(r.value, 17);
        label(right - 20, (box - 20) / 2, r.value, 17, white, 255, false, 1);
        label(right - 30 - vw, (box - 22) / 2, "\xe2\x80\xb9", 20, muted, 255, false, 1);
        right -= vw + 46;
      } else if (r.kind == Row::Nav) {
        label(right, (box - 22) / 2, "\xe2\x80\xba", 20, muted, 255, false, 1);
        right -= 22;
        if (!r.value.empty()) {
          label(right, (box - 20) / 2, r.value, 15, muted, 255, false, 1, 260);
          right -= std::min(260, measure(r.value, 15)) + 16;
        }
      } else if (r.kind == Row::Info) {
        label(right, (box - 20) / 2, r.value, 16, white, 255, false, 1, sheetW - 200);
        right = 180;
      }
      label(textX, labelY, r.label, labelSize, labelColor, 255, r.kind == Row::Primary, -1, std::max(60, right - textX - 8));
      if (!r.detail.empty())
        label(textX, labelY + 24, r.detail, 13, faint, 255, false, -1, sheetW - textX - 16);
      endLayer();
    }
    place(layer.image, sheetX, y);
  }
  if (int(rows.size()) > shown) {
    int trackH = shown * rowH - 6, thumb = std::max(24, trackH * shown / int(rows.size()));
    int offset = (trackH - thumb) * first / std::max(1, int(rows.size()) - shown);
    solidQuad(white, sheetX + sheetW + 10, sheetTop, 4, trackH, 0.1, 2);
    solidQuad(white, sheetX + sheetW + 10, sheetTop + offset, 4, thumb, 0.6, 2);
  }
  if (rowCache.size() > 300)
    for (auto it = rowCache.begin(); it != rowCache.end();)
      it = rowCache.size() > 150 ? rowCache.erase(it) : std::next(it);

  // Long text (reports, release notes): one cached layer per line.
  int y = rows.empty() ? sheetTop : sheetTop + rowsShown * rowH + 6;
  if (special == About) {
    place(icon("emblem-72"), sheetX, 84);
    place(icon("wordmark-46"), sheetX + 90, 92);
    std::string key = "about";
    if (beginLayer(headerLayer, key, sheetW, 70)) {
      label(0, 0, "Version " HARDRPG_VERSION, 16, white);
      label(0, 24, "Classic RPG games on the PS Vita. Made by Asukate.", 15, muted);
      label(0, 46, "github.com/Asukate/mkxp-z-vita  \xc2\xb7  powered by mkxp-z and EasyRPG Player  \xc2\xb7  Vita drawing by Tokyoship, CC BY 3.0", 13, faint, 255, false, -1, sheetW - 92);
      endLayer();
    }
    place(headerLayer.image, sheetX + 92, 144);
    y = 230;
    solidQuad(white, sheetX, y - 8, sheetW, 1, 0.12);
  }
  if (!body.empty()) {
    int lineH = 25;
    for (int i = 0; i < bodyLines && i + bodyFirst < int(body.size()); ++i) {
      auto &text = body[i + bodyFirst];
      std::string key = "body|" + text;
      auto &layer = rowCache[key];
      if (layer.key.empty()) {
        beginLayer(layer, key, sheetW, lineH);
        label(0, 2, text, 16, soft, 255, false, -1, sheetW);
        endLayer();
      }
      place(layer.image, sheetX, y + i * lineH);
    }
    if (int(body.size()) > bodyLines) {
      int trackH = bodyLines * lineH, thumb = std::max(24, trackH * bodyLines / int(body.size()));
      int offset = (trackH - thumb) * bodyFirst / std::max(1, int(body.size()) - bodyLines);
      solidQuad(white, sheetX + sheetW + 10, y, 4, trackH, 0.1, 2);
      solidQuad(white, sheetX + sheetW + 10, y + offset, 4, thumb, 0.6, 2);
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
      else
        solidQuad(white, x, ky, w, h, used ? 0.05 : keyRow ? 0.1 : 0.16, 6);
      std::string name = controlPicks[i].name, key = "key|" + name + "|" + std::to_string(w) + "|" + std::to_string(used && !here);
      auto &layer = rowCache[key];
      if (layer.key.empty()) {
        beginLayer(layer, key, w, h);
        int size = name.size() > 5 ? 12 : name.size() > 2 ? 14 : 17;
        label(w / 2, (h - size - 4) / 2, name, size, used && !here ? faint : white, 255, false, 0, w - 4);
        endLayer();
      }
      place(layer.image, x, ky);
    }
    y = 463;
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
      label(18, (h - 22) / 2, "Mode", 18, white);
      label(w - 16, (h - 22) / 2, "\xe2\x80\xb9  " + u.controlValue(0) + "  \xe2\x80\xba", 17, white, 255, false, 1);
      endLayer();
    }
    place(mode.image, x, py);
    // The surface as the player holds it.
    solidQuad(white, 214, 200, 668, 240, 0.04, 14);
    std::string surfaceKey = std::string("padsurface|") + (rear ? "rear" : "front");
    auto &caption = rowCache[surfaceKey];
    if (caption.key.empty()) {
      beginLayer(caption, surfaceKey, 640, 18);
      label(0, 0, rear ? "The back of the console, left and right as you hold it" : "The screen", 12, faint);
      endLayer();
    }
    place(caption.image, 228, 204);
    if (u.controlsCount() == 1) {
      int m = rear ? u.controls.rear : u.controls.front;
      std::string text = m == 2 ? (rear ? "Trackpad: hold L and slide to move the pointer." : "Mouse pointer: touches move and click the pointer.")
                                : "Off. Choose a mode with Left and Right to map zones.";
      auto &l = rowCache["padoff|" + text];
      if (l.key.empty()) {
        beginLayer(l, "padoff|" + text, 616, 24);
        label(0, 0, text, 17, muted);
        endLayer();
      }
      place(l.image, 240, 300);
    }
    for (int r = 1; r < u.controlsCount(); ++r) {
      u.padRowRect(r, x, py, w, h);
      bool here = u.setting == r;
      if (here)
        solidQuad(ruby, x, py, w, h, 0.9, 10);
      else
        solidQuad(white, x, py, w, h, 0.1, 10);
      std::string name = u.controlLabel(r);
      name = name.substr(name.find(' ') + 1);
      if (!name.empty())
        name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
      std::string zkey = "zone|" + name + "|" + u.controlValue(r) + "|" + std::to_string(w) + "x" + std::to_string(h);
      auto &zone = rowCache[zkey];
      if (zone.key.empty()) {
        beginLayer(zone, zkey, w, h);
        label(12, 10, name, 14, {230, 230, 232});
        label(12, h > 120 ? h / 2 - 16 : 34, u.controlValue(r), 22, white, 255, true, -1, w - 24);
        endLayer();
      }
      place(zone.image, x, py);
    }
    y = 450;
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
    glow(L, 290, 34, 300, 30);
    glow(R, 1510, 34, 300, 30);
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
        pressed += std::string(controlSources[i]) + "  \xe2\x86\x92  " + controlName(u.controls.buttons[i]) + "\n";
    std::string actions;
    for (auto &a : controlActions)
      if (a.code && u.controlsResult.actions[a.code])
        actions += std::string(a.name) + "  ";
    for (auto &k : controlPicks)
      if (controlIsKey(k.value) && u.controlsResult.keys[k.value - controlKeyBase])
        actions += std::string("Key ") + k.name + "  ";
    std::string key = "vita|" + pressed + "|" + actions + "|" + std::to_string(u.controls.enabled);
    if (beginLayer(bodyLayer, key, 220, 270)) {
      label(0, 0, "Back touch pad", 13, faint);
      label(0, 140, "Held", 13, faint);
      int ly = 160;
      if (pressed.empty())
        label(0, ly, "Press any button", 15, muted);
      for (size_t at = 0, next; at < pressed.size() && ly < 230; at = next + 1, ly += 20) {
        next = pressed.find('\n', at);
        label(0, ly, pressed.substr(at, next - at), 14, white, 255, false, -1, 210);
      }
      endLayer();
    }
    place(bodyLayer.image, px, py - 20);
    std::string screenKey = "vitascreen|" + std::to_string(smp.front.empty());
    auto &screenText = rowCache[screenKey];
    if (screenText.key.empty()) {
      beginLayer(screenText, screenKey, int(sw), 20);
      if (smp.front.empty())
        label(int(sw) / 2, 0, "Touch the screen", 14, faint, 255, false, 0);
      endLayer();
    }
    place(screenText.image, sx, sy + sh / 2 - 10);
    notes = {actions.empty() ? (u.controls.enabled ? "Nothing this game's profile does is held." : "Custom controls are off: this shows the raw buttons.")
                             : "In game this does: " + actions};
    y = by + bh + 14;
  }

  // Notes under everything else, wrapped to the column.
  if (!notes.empty()) {
    std::string key;
    for (auto &n : notes)
      key += n + "\n";
    if (notesLayer.key != key) {
      std::vector<std::string> lines;
      for (auto &n : notes) {
        auto wrapped = wrap(n, 15, sheetW);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        lines.push_back("");
      }
      if (!lines.empty())
        lines.pop_back();
      int h = std::max(1, int(lines.size()) * 21);
      beginLayer(notesLayer, key, sheetW, h);
      for (size_t i = 0; i < lines.size(); ++i)
        label(0, int(i) * 21, lines[i], 15, muted);
      endLayer();
    }
    // Notes never run into the hint bar: they move up if they must.
    int top = std::min(y + 2, hintsY - 6 - notesLayer.image.height);
    place(notesLayer.image, sheetX, std::max(sheetTop, top));
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
      label(480, 0, "Starting " + u.launchingName, 28, white, 255, true, 0, 900);
      label(480, 44, u.preparationLabel.empty() ? "Loading" : u.preparationLabel, 16, muted, 255, false, 0, 900);
      endLayer();
    }
    place(messageLayer.image, 0, 232);
  }
}
} // namespace hardrpg
