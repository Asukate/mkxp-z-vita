// The home screen: state, input and animation. Drawing is in home_view.cpp.
#include "home_layout.h"
#include "i18n.h"
#include "ui.h"
#include "util/json5pp.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace hardrpg {
using namespace homeLayout;
namespace {
constexpr int engineCount = 4; // formats 1..4: XP, VX, VX Ace, 2000/2003
constexpr int optionCount = 5; // favourite, artwork, all artwork, game settings, rename
} // namespace

std::vector<std::string> UI::artFolders(const Entry &e) const {
  // Art lives in art/<name>/ under any of these names; the slug spellings
  // are forgiving: lower case, runs of other characters as one dash.
  auto slug = [](const std::string &name) {
    std::string out;
    for (unsigned char c : name) {
      if (std::isalnum(c))
        out += char(std::tolower(c));
      else if (!out.empty() && out.back() != '-')
        out += '-';
    }
    while (!out.empty() && out.back() == '-')
      out.pop_back();
    return out;
  };
  auto path = e.node.path();
  while (!path.empty() && path.back() == '/')
    path.pop_back();
  auto folder = path.substr(path.find_last_of('/') + 1);
  return {entryKey(e), e.name, folder, slug(folder), slug(e.name)};
}

void UI::fetchArt(bool all) {
  if (artFetch && artFetch->running()) {
    if (!artQuiet) {
      message = tr("Game info is already downloading.");
      return;
    }
    // The background lookup gives way; its games are tried again next time.
    artFetch.reset();
  }
  artQuiet = false;
  artQuietKeys.clear();
  std::vector<ArtFetch::Job> jobs;
  auto add = [&](const Entry &e) {
    if (e.kind == Kind::Folder)
      return;
    auto folders = artFolders(e);
    // The game's files, for its title screen: a ZIP only once unpacked.
    std::string gameDir = e.node.archive ? model.paths.at("cache/zip-" + key(e.node.identity(model.paths))) : e.node.path();
    jobs.push_back({entryKey(e), e.name, folders[4].empty() ? folders[0] : folders[4], folders, gameDir});
  };
  if (all) {
    for (auto &e : model.list)
      add(e);
  } else if (auto *e = homeEntry())
    add(*e);
  if (jobs.empty()) {
    message = tr("Collections get game info for each game inside them.");
    return;
  }
  if (!artFetch)
    artFetch.reset(new ArtFetch);
  artFetch->start(std::move(jobs), model.paths.at("art/"), model.paths.at(""), all);
  message = all ? std::string(tr("Looking for game info...")) : trf("Looking for game info for {}...", {homeEntry()->name});
}

namespace {
std::set<std::string> readLines(const std::string &path) {
  std::set<std::string> out;
  std::ifstream in(path);
  for (std::string line; std::getline(in, line);)
    if (!line.empty())
      out.insert(line);
  return out;
}
} // namespace

void UI::fetchArtAutomatically() {
  if (!autoGameInfo || (artFetch && artFetch->running()))
    return;
  auto tried = readLines(model.paths.at("cache/game-info-tried.txt"));
  std::vector<ArtFetch::Job> jobs;
  std::vector<std::string> keys;
  for (auto &e : model.list) {
    if (e.kind == Kind::Folder || tried.count(entryKey(e)))
      continue;
    auto folders = artFolders(e);
    std::string gameDir = e.node.archive ? model.paths.at("cache/zip-" + key(e.node.identity(model.paths))) : e.node.path();
    jobs.push_back({entryKey(e), e.name, folders[4].empty() ? folders[0] : folders[4], folders, gameDir});
    keys.push_back(entryKey(e));
  }
  if (jobs.empty())
    return;
  artFetch.reset(new ArtFetch);
  artFetch->start(std::move(jobs), model.paths.at("art/"), model.paths.at(""), true);
  artQuiet = true;
  artQuietKeys = std::move(keys);
}

bool UI::pollArt() {
  if (!artFetch)
    return false;
  bool finished = !artFetch->running();
  auto p = artFetch->poll();
  for (auto &k : p.updated)
    artUpdated.push_back(k);
  if (artQuiet) {
    // Each game is looked up once; offline, they are all tried next time.
    if (finished && p.note.empty()) {
      auto path = model.paths.at("cache/game-info-tried.txt");
      auto tried = readLines(path);
      tried.insert(artQuietKeys.begin(), artQuietKeys.end());
      std::string text;
      for (auto &k : tried)
        text += k + "\n";
      try {
        atomicWrite(path, text);
      } catch (const std::exception &) {
      }
    }
    if (finished) {
      artFetch.reset();
      artQuiet = false;
      artQuietKeys.clear();
    }
    return !p.updated.empty();
  }
  ArtFetch::Choices offer;
  if (p.choosing && artFetch->takeChoices(offer)) {
    // Open the chooser on the current pictures (or the first offered).
    artChoices = std::move(offer);
    artRow = -1;
    for (int k = 0; k < 3; ++k) {
      auto &o = artChoices.options[k];
      artPick[k] = o.empty() ? -1 : 0;
      if (artRow < 0 && o.size() > 1)
        artRow = k;
      if (blurAdult)
        for (auto &c : o)
          if (c.adult)
            c.thumb = c.thumb.blurred(6);
    }
    home.panel = Home::Artwork;
    message.clear();
    return true;
  }
  if (p.choosing)
    return false;
  std::string text;
  if (!p.note.empty())
    text = tr(p.note);
  else if (p.finished)
    text = p.total == 1 ? std::string(tr(p.found ? "Game info and artwork saved." : "No game info found for this game."))
                        : trf("Game info found for {} of {} games.", {std::to_string(p.found), std::to_string(p.total)});
  else if (p.total > 1)
    text = trf("Downloading game info {} of {}: {}", {std::to_string(p.done + 1), std::to_string(p.total), p.current});
  else
    text = message;
  bool changed = text != message || !p.updated.empty();
  message = text;
  if (finished)
    artFetch.reset();
  return changed;
}

void UI::loadHome() {
  themeSourceCache.clear();
  activity = model.activity();
  favourites = model.favourites();
  int lang = -1;
  welcomeSeen = false;
  try {
    std::ifstream in(model.paths.at("launcher-preferences.json"));
    std::stringstream text;
    text << in.rdbuf();
    if (!text.str().empty()) {
      auto prefs = json5pp::parse(text.str());
      auto &o = prefs.as_object();
      // "blurAdult" (on by default before 0.4) is not read: the blur is now
      // off until chosen, saved as "blurAdultArtwork".
      if (o.count("blurAdultArtwork") && o.at("blurAdultArtwork").is_boolean())
        blurAdult = o.at("blurAdultArtwork").as_boolean();
      if (o.count("language") && o.at("language").is_string())
        lang = languageFromCode(o.at("language").as_string());
      if (o.count("welcomeSeen") && o.at("welcomeSeen").is_boolean())
        welcomeSeen = o.at("welcomeSeen").as_boolean();
      if (o.count("autoGameInfo") && o.at("autoGameInfo").is_boolean())
        autoGameInfo = o.at("autoGameInfo").as_boolean();
      if (o.count("homeGrid") && o.at("homeGrid").is_boolean())
        home.grid = o.at("homeGrid").as_boolean();
      if (o.count("menuSounds") && o.at("menuSounds").is_boolean())
        menuSounds = o.at("menuSounds").as_boolean();
      if (o.count("themeMusic") && o.at("themeMusic").is_boolean())
        themeMusic = o.at("themeMusic").as_boolean();
      auto level = [&](const char *name, int &v) {
        if (o.count(name) && o.at(name).is_integer())
          v = std::max(10, std::min(100, int(o.at(name).as_integer())));
      };
      level("menuSoundVolume", effectsVolume);
      level("musicVolume", musicVolume);
    }
  } catch (...) {
    // A damaged file only loses the preferences.
  }
  setLanguage(lang >= 0 ? lang : systemLanguage());
  if (!welcomeSeen && !welcome.active)
    openWelcome();
}
void UI::savePreferences() {
  Json prefs(std::initializer_list<Json::pair_type>{});
  auto &o = prefs.as_object();
  o["blurAdultArtwork"] = blurAdult;
  o["language"] = std::string(languageCode(language()));
  o["welcomeSeen"] = welcomeSeen;
  o["homeGrid"] = home.grid;
  o["autoGameInfo"] = autoGameInfo;
  o["menuSounds"] = menuSounds;
  o["menuSoundVolume"] = Json::integer_type(effectsVolume);
  o["themeMusic"] = themeMusic;
  o["musicVolume"] = Json::integer_type(musicVolume);
  try {
    atomicWrite(model.paths.at("launcher-preferences.json"), prefs.stringify());
  } catch (const std::exception &e) {
    message = e.what();
  }
}
void UI::selectLanguage(int lang) {
  setLanguage(lang);
  savePreferences();
  // Texts made before the change (RTP checks, messages) are made again.
  message.clear();
}
void UI::changeLanguage(int step) {
  selectLanguage((language() + step + languageCount) % languageCount);
}

bool UI::lastArtRow() const {
  for (int k = artRow + 1; k < 3; ++k)
    if (!artChoices.options[k].empty())
      return false;
  return true;
}
void UI::finishChoosing(bool save) {
  int keep[3] = {-1, -1, -1};
  if (artFetch)
    artFetch->choose(save ? artPick : keep);
  home.panel = Home::NoPanel;
  artChoices = ArtFetch::Choices{};
  message = tr(save ? "Saving..." : "Kept the current artwork and details.");
}
std::string UI::entryKey(const Entry &e) const { return key(e.node.identity(model.paths)); }

bool UI::homeActive() const {
  return !welcome.active && focus == Focus::Home && page == Page::Home && !gameOptions && !browser && errorReport.empty() &&
         releaseReport.empty() && missingRtp.empty() && !controlsTesting;
}

std::vector<int> UI::engineTabs() const {
  std::vector<int> tabs{0};
  for (int f = 1; f <= engineCount; ++f) {
    if (home.hiddenEngines & (1u << f))
      continue;
    for (auto &e : model.list)
      if (e.version == f) {
        tabs.push_back(f);
        break;
      }
  }
  return tabs;
}

std::vector<size_t> UI::homeList() const {
  // Look each game up once, then sort on plain numbers and names: this runs
  // every frame and on every input, so it must stay cheap with 100+ games.
  struct Row {
    size_t index;
    Activity stat;
    std::string name;
  };
  std::vector<Row> rows;
  for (auto i : visible()) {
    auto &e = model.list[i];
    if (e.version && (home.hiddenEngines & (1u << e.version)))
      continue;
    auto k = entryKey(e);
    auto a = activity.find(k);
    Activity stat = a == activity.end() ? Activity{} : a->second;
    if (home.unplayedOnly && stat.seconds > 0)
      continue;
    if (home.favouritesOnly && !favourites.count(k))
      continue;
    rows.push_back({i, stat, home.sort == Home::Name ? lower(e.name) : std::string()});
  }
  // Stable sorts keep the library order between equal games.
  if (home.sort == Home::Name)
    std::stable_sort(rows.begin(), rows.end(), [&](const Row &a, const Row &b) { return home.direction * a.name.compare(b.name) < 0; });
  else if (home.sort == Home::PlayTime)
    std::stable_sort(rows.begin(), rows.end(), [&](const Row &a, const Row &b) {
      return home.direction > 0 ? a.stat.seconds > b.stat.seconds : a.stat.seconds < b.stat.seconds;
    });
  else
    std::stable_sort(rows.begin(), rows.end(), [&](const Row &a, const Row &b) {
      return home.direction > 0 ? a.stat.last > b.stat.last : a.stat.last < b.stat.last;
    });
  std::vector<size_t> out;
  out.reserve(rows.size());
  for (auto &r : rows)
    out.push_back(r.index);
  return out;
}

const Entry *UI::homeEntry() const {
  auto list = homeList();
  if (home.target < 0 || size_t(home.target) >= list.size())
    return nullptr;
  return &model.list[list[home.target]];
}

int UI::filterRows() const { return engineCount + 2 + Home::SortCount + 1; }

void UI::filterActivate(int row) {
  auto *current = homeEntry();
  std::string keep = current ? entryKey(*current) : "";
  if (row < engineCount) {
    unsigned bit = 1u << (row + 1);
    home.hiddenEngines ^= bit;
    // Never hide every engine.
    if ((home.hiddenEngines & 0x1e) == 0x1e)
      home.hiddenEngines ^= bit;
    if (engineFilter && (home.hiddenEngines & (1u << engineFilter)))
      engineFilter = 0;
  } else if (row == engineCount)
    home.favouritesOnly = !home.favouritesOnly;
  else if (row == engineCount + 1)
    home.unplayedOnly = !home.unplayedOnly;
  else if (row < engineCount + 2 + Home::SortCount) {
    int sort = row - engineCount - 2;
    if (home.sort == sort)
      home.direction = -home.direction;
    else {
      home.sort = sort;
      home.direction = 1;
    }
  } else {
    home.hiddenEngines = 0;
    home.favouritesOnly = home.unplayedOnly = false;
    home.sort = Home::Recent;
    home.direction = 1;
    engineFilter = 0;
  }
  // Keep the same game focused when it is still shown.
  auto list = homeList();
  home.target = 0;
  for (size_t i = 0; i < list.size(); ++i)
    if (entryKey(model.list[list[i]]) == keep)
      home.target = int(i);
  home.position = home.target;
}

namespace {
void clampTarget(Home &h, int count) {
  // The Add games card sits after the last game.
  h.target = std::max(0, std::min(h.target, count));
}
} // namespace

void UI::homeAction(Button b) {
  auto list = homeList();
  int count = int(list.size());
  auto tabs = engineTabs();
  auto switchTab = [&](int step) {
    int at = 0;
    for (size_t i = 0; i < tabs.size(); ++i)
      if (tabs[i] == engineFilter)
        at = int(i);
    engineFilter = tabs[(at + step + tabs.size()) % tabs.size()];
    home.target = 0;
    home.position = 0;
    home.details = false;
  };
  auto launchTarget = [&] {
    if (home.target >= count) {
      openBrowser(BrowseMode::Games);
      return;
    }
    play(list[home.target]);
  };
  auto runSearch = [&] {
    if (!searchText)
      return;
    std::string out;
    if (searchText(query, out)) {
      search(out);
      home.target = 0;
      home.position = 0;
    }
  };

  if (home.panel == Home::Artwork) {
    auto rows = [&](int from, int step) {
      for (int k = from + step; k >= 0 && k < 3; k += step)
        if (!artChoices.options[k].empty())
          return k;
      return from;
    };
    int n = artRow >= 0 ? int(artChoices.options[artRow].size()) : 0;
    if (b == Button::Up)
      artRow = rows(artRow, -1);
    else if (b == Button::Down)
      artRow = rows(artRow, 1);
    else if ((b == Button::Left || b == Button::Right) && n)
      artPick[artRow] = (artPick[artRow] + (b == Button::Left ? n - 1 : 1)) % n;
    else if (b == Button::Cross) {
      // Cross takes this row's pick and goes on; on the last row it saves.
      int next = rows(artRow, 1);
      if (next != artRow)
        artRow = next;
      else
        finishChoosing(true);
    } else if (b == Button::Triangle)
      finishChoosing(true);
    else if (b == Button::Circle)
      finishChoosing(false);
    return;
  }
  if (home.panel == Home::Filter) {
    int rows = filterRows();
    if (b == Button::Up || b == Button::Left)
      home.panelFocus = (home.panelFocus + rows - 1) % rows;
    else if (b == Button::Down || b == Button::Right)
      home.panelFocus = (home.panelFocus + 1) % rows;
    else if (b == Button::Cross)
      filterActivate(home.panelFocus);
    else if (b == Button::Circle || b == Button::Triangle)
      home.panel = Home::NoPanel;
    return;
  }
  if (home.panel == Home::Options) {
    if (b == Button::Up)
      home.panelFocus = (home.panelFocus + optionCount - 1) % optionCount;
    else if (b == Button::Down)
      home.panelFocus = (home.panelFocus + 1) % optionCount;
    else if (b == Button::Circle || b == Button::Triangle)
      home.panel = Home::NoPanel;
    else if (b == Button::Cross) {
      home.panel = Home::NoPanel;
      auto *e = homeEntry();
      if (!e)
        return;
      if (home.panelFocus == 0) {
        auto id = entryKey(*e);
        bool on = !favourites.count(id);
        model.setFavourite(id, on);
        favourites = model.favourites();
        message = tr(on ? "Added to favourites." : "Removed from favourites.");
        // A favourites-only list can lose the game; keep the index in range.
        clampTarget(home, int(homeList().size()));
      } else if (home.panelFocus == 1 || home.panelFocus == 2) {
        fetchArt(home.panelFocus == 2);
      } else if (home.panelFocus == 3) {
        openGameMenu(list[home.target]);
      } else {
        if (e->kind == Kind::Folder)
          throw std::runtime_error(tr("Open this collection to rename its games."));
        renameGame(*e);
      }
    }
    return;
  }
  // A screenshot full screen: Left and Right page, anything else closes it.
  if (home.viewing >= 0) {
    if (b == Button::Left || b == Button::Right) {
      int n = std::max(1, shotsShown);
      home.viewing = (home.viewing + (b == Button::Left ? n - 1 : 1)) % n;
      home.shotFocus = home.viewing;
    } else if (b == Button::Circle || b == Button::Cross || b == Button::Square)
      home.viewing = -1;
    return;
  }
  // The details panel's screenshot row: Down goes to it, Up leaves it.
  if (home.details && home.zone == Home::Shelf && !home.grid) {
    if (home.shotFocus >= 0) {
      int n = shotsShown;
      if (n <= 0)
        home.shotFocus = -1;
      else if (b == Button::Left || b == Button::Right) {
        home.shotFocus = std::max(0, std::min(n - 1, home.shotFocus + (b == Button::Left ? -1 : 1)));
        return;
      } else if (b == Button::Up || b == Button::Circle) {
        home.shotFocus = -1;
        return;
      } else if (b == Button::Cross) {
        home.viewing = home.shotFocus;
        return;
      } else if (b == Button::Down)
        return;
    } else if (b == Button::Down && shotsShown > 0) {
      home.shotFocus = 0;
      return;
    }
  }
  if (home.zone == Home::Top) {
    if (b == Button::Left)
      home.topFocus = 0;
    else if (b == Button::Right)
      home.topFocus = 1;
    else if (b == Button::Down || b == Button::Circle)
      home.zone = Home::Toolbar;
    else if (b == Button::Cross) {
      if (home.topFocus == 0)
        focus = Focus::About;
      else {
        focus = Focus::Settings;
        openPage(Page::Home);
      }
      home.zone = Home::Shelf;
    }
    return;
  }
  if (home.zone == Home::Toolbar) {
    if (b == Button::Left)
      home.toolbarFocus = std::max(0, home.toolbarFocus - 1);
    else if (b == Button::Right)
      home.toolbarFocus = std::min(3, home.toolbarFocus + 1);
    else if (b == Button::Up)
      home.zone = Home::Top;
    else if (b == Button::Down || b == Button::Circle)
      home.zone = Home::Shelf;
    else if (b == Button::L || b == Button::R)
      switchTab(b == Button::L ? -1 : 1);
    else if (b == Button::Cross) {
      if (home.toolbarFocus == 0) {
        home.grid = !home.grid;
        home.details = false;
        savePreferences(); // the library opens in this view from now on
      } else if (home.toolbarFocus == 1)
        openBrowser(BrowseMode::Games);
      else if (home.toolbarFocus == 2)
        runSearch();
      else {
        home.panel = Home::Filter;
        home.panelFocus = 0;
      }
    }
    return;
  }

  // Shelf.
  int cols = home.grid ? gridCols : 1;
  switch (b) {
  case Button::Left:
    home.target = std::max(0, home.target - 1);
    break;
  case Button::Right:
    home.target = std::min(count, home.target + 1);
    break;
  case Button::Down:
    if (home.grid)
      home.target = std::min(count, home.target + cols);
    break;
  case Button::Up:
    if (home.grid && home.target >= cols)
      home.target -= cols;
    else if (!home.details) {
      home.zone = Home::Toolbar;
      home.toolbarFocus = 3;
    }
    break;
  case Button::Cross:
    // In the carousel the first press shows the game's details, the second
    // plays it, as tapping does. The grid has no details and a collection
    // has none to show: those open at once.
    if (!home.grid && home.target < count && !home.details && model.list[list[home.target]].kind != Kind::Folder)
      home.details = true;
    else
      launchTarget();
    break;
  case Button::Square:
    if (!home.grid && home.target < count)
      home.details = !home.details;
    break;
  case Button::Triangle:
    if (home.target < count) {
      home.panel = Home::Options;
      home.panelFocus = 0;
    }
    break;
  case Button::Circle:
    if (home.details)
      home.details = false;
    else if (!query.empty()) {
      search("");
      home.target = 0;
    } else if (model.back()) {
      home.target = 0;
      home.position = 0;
    }
    break;
  case Button::L:
  case Button::R:
    switchTab(b == Button::L ? -1 : 1);
    break;
  case Button::Select:
    runSearch();
    break;
  case Button::Start:
    openBrowser(BrowseMode::Games);
    break;
  default:
    break;
  }
  if (home.target >= count)
    home.details = false;
  clampTarget(home, count);
  if (!home.details)
    home.shotFocus = home.viewing = -1;
  if (home.grid)
    home.position = home.target;
}

void UI::homeTap(int x, int y) {
  if (home.viewing >= 0) {
    if (y < hintsY && x < 160)
      homeAction(Button::Left);
    else if (y < hintsY && x >= 800)
      homeAction(Button::Right);
    else if (y < hintsY)
      homeAction(Button::Circle);
    else
      for (auto &h : hintHit)
        if (inside(x, y, h.x, hintsY, h.w, 544 - hintsY))
          homeAction(h.button);
    return;
  }
  auto list = homeList();
  int count = int(list.size());
  if (home.panel == Home::Artwork) {
    for (int k = 0; k < 3; ++k)
      for (int i = 0; i < int(artChoices.options[k].size()); ++i) {
        int rx, ry, rw, rh;
        pickerSlot(k, i, rx, ry, rw, rh);
        if (inside(x, y, rx, ry, rw, rh + pickerLabel)) {
          artRow = k;
          artPick[k] = i;
          return;
        }
      }
    if (y >= hintsY)
      for (auto &h : hintHit)
        if (inside(x, y, h.x, hintsY, h.w, 544 - hintsY))
          homeAction(h.button);
    return;
  }
  if (home.panel == Home::Filter) {
    if (!inside(x, y, panelX, panelY, panelW, hintsY - panelY)) {
      home.panel = Home::NoPanel;
      return;
    }
    // Rows are laid out by the view; it keeps the hit boxes in filterHit.
    for (int row = 0; row < filterRows(); ++row) {
      auto &r = filterHit[row];
      if (inside(x, y, r[0], r[1], r[2], r[3])) {
        home.panelFocus = row;
        filterActivate(row);
        return;
      }
    }
    return;
  }
  if (home.panel == Home::Options) {
    if (inside(x, y, menuX, menuY + 30, menuW, optionCount * menuRowH)) {
      home.panelFocus = (y - menuY - 30) / menuRowH;
      homeAction(Button::Cross);
    } else
      home.panel = Home::NoPanel;
    return;
  }
  home.zone = Home::Shelf;
  if (y < topHeight) {
    if (inside(x, y, gearX, topIconY, topIcon, topIcon)) {
      home.zone = Home::Top;
      home.topFocus = 1;
      homeAction(Button::Cross);
    } else if (inside(x, y, aboutX, topIconY, topIcon, topIcon)) {
      home.zone = Home::Top;
      home.topFocus = 0;
      homeAction(Button::Cross);
    } else if (x < 300) {
      showFrameTime = !showFrameTime;
      frameText.clear();
    }
    return;
  }
  if (y >= barY && y < barY + barH && !home.details) {
    int item = inside(x, y, viewX, barY, barIcon, barH) ? 0 : inside(x, y, addX, barY, addW, barH) ? 1
             : inside(x, y, searchX, barY, barIcon, barH) ? 2 : inside(x, y, filterX, barY, barIcon, barH) ? 3 : -1;
    if (item >= 0) {
      home.zone = Home::Toolbar;
      home.toolbarFocus = item;
      homeAction(Button::Cross);
      home.zone = Home::Shelf;
      return;
    }
    for (auto &t : tabHit)
      if (x >= t.x && x < t.x + t.w) {
        engineFilter = t.engine;
        home.target = 0;
        home.position = 0;
      }
    return;
  }
  if (y >= hintsY) {
    // The hint bar's buttons act like the buttons they show.
    for (auto &h : hintHit)
      if (inside(x, y, h.x, hintsY, h.w, 544 - hintsY)) {
        homeAction(h.button);
        return;
      }
    return;
  }
  if (home.grid) {
    int col = (x - gridX) / (cardW + gridGapX), row = (y - gridTop + gridScroll) / (cardH + gridLabel + gridGapY);
    if (x < gridX || col >= gridCols || y < gridTop)
      return;
    int i = row * gridCols + col;
    if (i > count)
      return;
    if (i == home.target)
      homeAction(Button::Cross);
    else
      home.target = home.position = i;
    return;
  }
  if (home.details) {
    for (int i = 0; i < shotsShown && i < 4; ++i)
      if (inside(x, y, shotHit[i][0], shotHit[i][1], shotHit[i][2], shotHit[i][3])) {
        home.shotFocus = home.viewing = i;
        return;
      }
    if (inside(x, y, playX, playY, playW, buttonH)) {
      homeAction(Button::Cross);
      return;
    }
    if (inside(x, y, optionsX, playY, optionsW, buttonH)) {
      homeAction(Button::Triangle);
      return;
    }
  }
  // Cards, nearest first so the focused card wins overlaps.
  int best = -1;
  double bestNear = -1;
  for (int i = std::max(0, int(home.position) - 4); i <= std::min(count, int(home.position) + 4); ++i) {
    auto r = card(i, home.position, home.detailsAmount, home.sideAmount);
    if (home.details && i > home.target)
      continue;
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h && r.nearness > bestNear) {
      best = i;
      bestNear = r.nearness;
    }
  }
  if (best < 0)
    return;
  if (best != home.target)
    home.target = best;
  else if (best == count)
    openBrowser(BrowseMode::Games);
  else if (!home.details)
    home.details = true;
  else
    homeAction(Button::Cross);
}

bool UI::homePointer(int state, int x, int y, uint32_t ms) {
  int count = int(homeList().size());
  if (state == 0) {
    home.pressed = true;
    home.dragging = false;
    home.pressX = home.lastX = x;
    home.pressY = y;
    home.dragFrom = home.position;
    home.velocity = 0;
    home.lastMove = ms;
    return false;
  }
  if (!home.pressed)
    return false;
  bool shelfDrag = home.panel == Home::NoPanel && home.viewing < 0 && home.pressY >= barY + barH && home.pressY < hintsY;
  if (state == 1) {
    if (!home.dragging && shelfDrag && std::abs(x - home.pressX) > 10 && !home.grid)
      home.dragging = true;
    if (!home.dragging && shelfDrag && home.grid && std::abs(y - home.pressY) > 10)
      home.dragging = true;
    if (!home.dragging)
      return false;
    double dt = std::max(1u, ms - home.lastMove) / 1000.0;
    if (home.grid) {
      gridScroll = std::max(0, gridScroll - (y - home.pressY));
      home.pressY = y;
      return true;
    }
    double p = home.dragFrom - double(x - home.pressX) / cardGap;
    // Rubber band past either end.
    if (p < 0)
      p *= 0.35;
    if (p > count)
      p = count + (p - count) * 0.35;
    double v = -double(x - home.lastX) / cardGap / dt;
    home.velocity = home.velocity * 0.4 + v * 0.6;
    // A tick each time a card passes the middle.
    if (std::lround(p) != std::lround(home.position) && p >= -0.5 && p <= count + 0.5)
      emit(Sound::Move);
    home.position = p;
    home.lastX = x;
    home.lastMove = ms;
    return true;
  }
  // Release.
  home.pressed = false;
  if (home.dragging) {
    home.dragging = false;
    if (!home.grid) {
      int before = home.target;
      home.target = int(std::lround(home.position + home.velocity * 0.18));
      clampTarget(home, count);
      if (home.target != int(std::lround(home.position)) && home.target != before)
        emit(Sound::Move);
      home.details = home.details && home.target < count;
    }
    return true;
  }
  // A tap: through touch(), so it sounds like the button it stands for.
  touch(home.pressX, home.pressY);
  return true;
}

bool UI::animating() const {
  // The guide's slides animate for a moment; the view previews keep moving.
  if (welcome.active)
    return welcome.amount < 1 || welcome.clock < 2.5 || welcome.slide == Welcome::View;
  if (!homeActive())
    return false;
  return home.dragging || std::fabs(home.target - home.position) > 0.002 ||
         std::fabs((home.details ? 1.0 : 0.0) - home.detailsAmount) > 0.002 ||
         std::fabs((home.panel == Home::Filter ? 1.0 : 0.0) - home.sideAmount) > 0.002;
}

void UI::tick(double seconds) {
  if (welcome.active) {
    welcome.clock += seconds;
    // A slide change takes about a quarter of a second.
    if (welcome.amount < 1) {
      welcome.amount = std::min(1.0, welcome.amount + seconds / 0.26);
      if (welcome.amount >= 1)
        welcome.from = -1;
    }
  }
  if (home.dragging)
    return;
  double k = 1 - std::exp(-seconds * 13.0);
  home.position += (home.target - home.position) * k;
  if (std::fabs(home.target - home.position) < 0.002)
    home.position = home.target;
  double want = home.details ? 1.0 : 0.0;
  home.detailsAmount += (want - home.detailsAmount) * (1 - std::exp(-seconds * 16.0));
  if (std::fabs(want - home.detailsAmount) < 0.002)
    home.detailsAmount = want;
  double side = home.panel == Home::Filter ? 1.0 : 0.0;
  home.sideAmount += (side - home.sideAmount) * (1 - std::exp(-seconds * 16.0));
  if (std::fabs(side - home.sideAmount) < 0.002)
    home.sideAmount = side;
}
} // namespace hardrpg
