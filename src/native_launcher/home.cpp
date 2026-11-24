// The home screen: state, input and animation. Drawing is in home_view.cpp.
#include "home_layout.h"
#include "i18n.h"
#include "ui.h"
#include "util/json5pp.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace hardrpg {
using namespace homeLayout;
namespace {
constexpr int engineCount = filterEngineCount;
constexpr int optionCount = 5; // favourite, artwork, all artwork, game settings, rename
const char *const viewNames[] = {"carousel", "grid", "list"};
int viewFromName(const std::string &name, int otherwise) {
  for (int v = 0; v < Home::ViewCount; ++v)
    if (name == viewNames[v])
      return v;
  return otherwise;
}
} // namespace

void UI::setView(int view, bool remember) {
  home.view = view;
  home.details = home.achievementFocus = false;
  home.shotFocus = home.viewing = -1;
  home.position = home.target;
  home.gridVelocity = 0;
  home.gridLift = 1.0;
  home.gridLiftFor = home.target;
  followList();
  followGrid();
  if (remember) {
    lastView = view;
    savePreferences();
  }
}
int UI::gridMaxScroll(int count) const {
  int visibleH = hintsY - gridTop;
  int rows = count / gridCols + 1;
  return std::max(0, rows * gridRowH - visibleH + 16);
}
void UI::followGrid() {
  if (!home.grid())
    return;
  int count = int(homeList().size());
  int row = home.target / gridCols;
  int topY = row * gridRowH;
  int visibleH = hintsY - gridTop;
  if (topY < gridScroll)
    gridScroll = topY;
  else if (topY + gridRowH > gridScroll + visibleH)
    gridScroll = topY + gridRowH - visibleH;
  gridScroll = std::max(0, std::min(gridScroll, gridMaxScroll(count)));
}
void UI::followList() {
  if (!home.list())
    return;
  int total = int(homeList().size()) + 1, y = home.target * listRowH;
  if (y < listScroll)
    listScroll = y;
  else if (y + listRowH > listScroll + listRows * listRowH)
    listScroll = y + listRowH - listRows * listRowH;
  listScroll = std::max(0, std::min(listScroll, (total - listRows) * listRowH));
}
namespace {
// Runs followList/followGrid when the input handler it guards returns.
// Grid only scrolls when the selection actually moved, so touch scrolling stays put.
struct FollowList {
  UI &ui;
  int beforeTarget;
  explicit FollowList(UI &u) : ui(u), beforeTarget(u.home.target) {}
  ~FollowList() {
    ui.followList();
    if (ui.home.target != beforeTarget) {
      ui.home.gridVelocity = 0;
      ui.followGrid();
    }
  }
};
} // namespace

void UI::changeStartView(int step) {
  // -1 (last used), then each view, round and round.
  startView = (startView + 1 + step + Home::ViewCount + 1) % (Home::ViewCount + 1) - 1;
  if (startView >= 0)
    setView(startView, true);
  else
    savePreferences();
}

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
  detailsAchievementKey.clear();
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
      // The view used last ("homeView"; before the list view, "homeGrid"),
      // and the one chosen in Settings to open in, if any.
      lastView = Home::Carousel;
      if (o.count("homeView") && o.at("homeView").is_string())
        lastView = viewFromName(o.at("homeView").as_string(), Home::Carousel);
      else if (o.count("homeGrid") && o.at("homeGrid").is_boolean() && o.at("homeGrid").as_boolean())
        lastView = Home::Grid;
      startView = -1;
      if (o.count("homeStartView") && o.at("homeStartView").is_string())
        startView = viewFromName(o.at("homeStartView").as_string(), -1);
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
      if (o.count("saveBackupVersions") && o.at("saveBackupVersions").is_integer())
        for (int k : SaveBackups::keepChoices)
          if (k == o.at("saveBackupVersions").as_integer())
            backupKeep = k;
    }
  } catch (...) {
    // A damaged file only loses the preferences.
  }
  home.view = startView >= 0 ? startView : lastView;
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
  o["homeView"] = std::string(viewNames[lastView]);
  o["homeStartView"] = std::string(startView >= 0 ? viewNames[startView] : "last");
  o["homeGrid"] = lastView == Home::Grid; // read by 0.4.0
  o["autoGameInfo"] = autoGameInfo;
  o["menuSounds"] = menuSounds;
  o["menuSoundVolume"] = Json::integer_type(effectsVolume);
  o["themeMusic"] = themeMusic;
  o["musicVolume"] = Json::integer_type(musicVolume);
  o["saveBackupVersions"] = Json::integer_type(backupKeep);
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
  for (int f : runnableFormats) {
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

const GameAchievements &UI::detailsAchievements(const Entry &source) const {
  auto k = entryKey(source);
  if (detailsAchievementKey != k) {
    detailsAchievementKey = std::move(k);
    detailsAchievementData = {};
    try {
      Entry game;
      if (configurableGame(source, game))
        detailsAchievementData = gameAchievements(model.paths, game);
    } catch (const std::exception &) {
      // A collection or an unreadable catalog should leave the details usable.
    }
  }
  return detailsAchievementData;
}

void UI::openDetailsAchievements(const Entry &entry) {
  auto &progress = detailsAchievements(entry);
  if (!progress.available)
    return;
  gameEntry = entry;
  achievements = progress;
  achievementsFromDetails = gameOptions = true;
  home.achievementFocus = true;
  home.shotFocus = -1;
  page = Page::Achievements;
  focus = Focus::Settings;
  setting = settingFirst = achievementScroll = 0;
  message.clear();
}

void UI::followAchievement() {
  using namespace homeLayout;
  const int n = int(achievements.entries.size()), unlocked = achievements.unlockedRows();
  const int view = achListBottom - achListTop, end = achievementRowY(n, unlocked, n);
  if (n > 0) {
    setting = std::max(0, std::min(n - 1, setting));
    // The first of a section brings its heading into view with it.
    int top = achievementRowY(setting, unlocked, n), bottom = top + achRowH;
    if (setting == 0 || setting == unlocked)
      top -= achSectionH;
    if (top < achievementScroll)
      achievementScroll = top;
    else if (bottom > achievementScroll + view)
      achievementScroll = bottom - view;
  }
  achievementScroll = std::max(0, std::min(achievementScroll, std::max(0, end - view)));
}

namespace {
// The Steam app ID the artwork lookup found for a game (info.json), or 0.
long artSteamApp(const Paths &paths, const std::vector<std::string> &folders) {
  for (auto &folder : folders) {
    if (folder.empty())
      continue;
    try {
      auto info = json5pp::parse(readFile(paths.at("art/" + folder + "/info.json"), 64 * 1024));
      auto it = info.as_object().find("steamAppId");
      if (it != info.as_object().end() && it->second.is_string() && !it->second.as_string().empty())
        return std::atol(it->second.as_string().c_str());
    } catch (const std::exception &) {
    }
  }
  return 0;
}
} // namespace

void UI::fetchAchievementsAutomatically() {
  // Only games that use achievements: started with the bridge, shipping the
  // Steam script the bridge answers, or given a catalog by hand. Each is
  // looked up once, its missing icons again.
  for (auto &e : model.list) {
    if (e.kind == Kind::Folder || !(achievementsUsed(model.paths, e) || steamScriptGame(model.paths, e)))
      continue;
    auto game = key(e.node.identity(model.paths));
    if (steamCatalogComplete(model.paths.at("achievements/" + game)))
      continue;
    bool queued = false;
    for (auto &j : achievementQueue)
      queued = queued || j.game == game;
    if (!queued)
      achievementQueue.push_back({game, e.name, artSteamApp(model.paths, artFolders(e)), false});
  }
}

void UI::lookUpAchievements(const Entry &e) {
  auto game = key(e.node.identity(model.paths));
  for (auto it = achievementQueue.begin(); it != achievementQueue.end();)
    it = it->game == game ? achievementQueue.erase(it) : std::next(it);
  achievementQueue.push_back({game, e.name, artSteamApp(model.paths, artFolders(e)), true});
  message = tr("Looking up achievements on Steam...");
}

bool UI::pollAchievements() {
  bool changed = false;
  if (achievementFetch) {
    bool finished = !achievementFetch->running();
    auto p = achievementFetch->poll();
    for (auto &game : p.updated) {
      detailsAchievementKey.clear();
      if (page == Page::Achievements && achievements.game == game) {
        achievements = gameAchievements(model.paths, gameEntry);
        followAchievement();
      }
      changed = true;
    }
    if (!finished)
      return changed;
    achievementFetch.reset();
    if (page == Page::Achievements && message == tr("Looking up achievements on Steam...")) {
      message = tr(!p.found              ? "Could not reach Steam. Try again later."
                   : achievements.total ? "Achievements updated."
                                        : "No achievements found on Steam.");
      changed = true;
    }
  }
  // One download at a time: after the artwork lookup.
  if (achievementQueue.empty() || (artFetch && artFetch->running()))
    return changed;
  achievementFetch.reset(new AchievementFetch);
  achievementFetch->start(std::move(achievementQueue), model.paths.at("achievements/"), steamLanguage(language()));
  achievementQueue.clear();
  return changed;
}

int UI::filterRows() const { return engineCount + 2 + Home::SortCount + 1; }

void UI::filterActivate(int row) {
  auto *current = homeEntry();
  std::string keep = current ? entryKey(*current) : "";
  if (row < engineCount) {
    unsigned bit = 1u << filterEngineFormat(row), all = 0;
    for (int i = 0; i < engineCount; ++i)
      all |= 1u << filterEngineFormat(i);
    home.hiddenEngines ^= bit;
    // Never hide every engine.
    if ((home.hiddenEngines & all) == all)
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
  home.gridVelocity = 0;
  followGrid();
}

namespace {
void clampTarget(Home &h, int count) {
  // The Add games card sits after the last game.
  h.target = std::max(0, std::min(h.target, count));
}
} // namespace

void UI::homeAction(Button b) {
  FollowList follow{*this};
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
  // A screenshot full screen: Left/Right or L/R page, anything else closes it.
  if (home.viewing >= 0) {
    if (b == Button::Left || b == Button::Right || b == Button::L || b == Button::R) {
      int n = std::max(1, shotsShown);
      bool prev = b == Button::Left || b == Button::L;
      home.viewing = (home.viewing + (prev ? n - 1 : 1)) % n;
      home.shotFocus = home.viewing;
    } else if (b == Button::Circle || b == Button::Cross || b == Button::Square)
      home.viewing = -1;
    return;
  }
  // Move down from the game to achievement progress and then screenshots.
  // The details panel shows in the carousel and the grid.
  if (home.details && home.zone == Home::Shelf && !home.list() && home.target < count) {
    const auto &entry = model.list[list[home.target]];
    const bool hasProgress = detailsAchievements(entry).available;
    if (home.achievementFocus) {
      if (hasProgress && b == Button::Cross) {
        openDetailsAchievements(entry);
        return;
      }
      if (hasProgress && b == Button::Down) {
        if (shotsShown > 0) {
          home.achievementFocus = false;
          home.shotFocus = 0;
        }
        return;
      }
      home.achievementFocus = false;
      if (hasProgress && (b == Button::Up || b == Button::Circle))
        return;
    }
    if (home.shotFocus >= 0) {
      int n = shotsShown;
      if (n <= 0)
        home.shotFocus = -1;
      else if (b == Button::Left || b == Button::Right) {
        home.shotFocus = std::max(0, std::min(n - 1, home.shotFocus + (b == Button::Left ? -1 : 1)));
        return;
      } else if (b == Button::Up || b == Button::Circle) {
        home.shotFocus = -1;
        home.achievementFocus = b == Button::Up && hasProgress;
        return;
      } else if (b == Button::Cross) {
        home.viewing = home.shotFocus;
        return;
      } else if (b == Button::Down)
        return;
    } else if (b == Button::Down && (hasProgress || shotsShown > 0)) {
      home.achievementFocus = hasProgress;
      if (!hasProgress)
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
      if (home.toolbarFocus == 0)
        setView((home.view + 1) % Home::ViewCount, true); // carousel, grid, list
      else if (home.toolbarFocus == 1)
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

  // Shelf. In the list Up and Down go a row, Left and Right a page.
  bool carousel = home.view == Home::Carousel;
  bool hasDetails = !home.list();
  int cols = home.grid() && !home.details ? gridCols : 1;
  int sideStep = home.list() ? listRows : 1;
  switch (b) {
  case Button::Left:
    home.target = std::max(0, home.target - sideStep);
    break;
  case Button::Right:
    home.target = std::min(count, home.target + sideStep);
    break;
  case Button::Down:
    if (!carousel && !home.details)
      home.target = std::min(count, home.target + cols);
    break;
  case Button::Up:
    if (!carousel && !home.details && home.target >= cols)
      home.target -= cols;
    else if (!home.details) {
      home.zone = Home::Toolbar;
      home.toolbarFocus = 3;
    }
    break;
  case Button::Cross:
    // In carousel and grid the first press shows the game's details, the second
    // plays it, as tapping does. The list and collections open at once.
    if (hasDetails && home.target < count && !home.details && model.list[list[home.target]].kind != Kind::Folder) {
      home.details = true;
      detailsAchievementKey.clear();
    } else
      launchTarget();
    break;
  case Button::Square:
    if (hasDetails && home.target < count && model.list[list[home.target]].kind != Kind::Folder) {
      home.details = !home.details;
      if (home.details)
        detailsAchievementKey.clear();
    }
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
  if (!home.details) {
    home.achievementFocus = false;
    home.shotFocus = home.viewing = -1;
  }
  if (!carousel)
    home.position = home.target;
}

void UI::homeTap(int x, int y) {
  FollowList follow{*this};
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
  if (home.list()) {
    if (x < listX || x >= listX + listW || y < listTop || y >= listBottom)
      return;
    int i = (y - listTop + listScroll) / listRowH;
    if (i > count)
      return;
    if (i == home.target)
      homeAction(Button::Cross);
    else
      home.target = home.position = i;
    return;
  }
  if (home.details && !home.list() && home.target < count) {
    const auto &entry = model.list[list[home.target]];
    const int xStart = detailsX + int((1 - home.detailsAmount) * 24);
    if (inside(x, y, xStart, progressY(shotsShown > 0), progressW, progressH) &&
        detailsAchievements(entry).available) {
      openDetailsAchievements(entry);
      return;
    }
    for (int i = 0; i < shotsShown && i < 4; ++i)
      if (inside(x, y, shotHit[i][0], shotHit[i][1], shotHit[i][2], shotHit[i][3])) {
        home.achievementFocus = false;
        home.shotFocus = home.viewing = i;
        return;
      }
    if (inside(x, y, playX, playY, playW, buttonH)) {
      home.achievementFocus = false;
      home.shotFocus = -1;
      homeAction(Button::Cross);
      return;
    }
    if (inside(x, y, optionsX, playY, optionsW, buttonH)) {
      homeAction(Button::Triangle);
      return;
    }
    if (home.grid()) {
      auto r = card(0, 0, 1.0, 0);
      if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
        homeAction(Button::Cross);
      return;
    }
  }
  if (home.grid()) {
    int col = (x - gridX) / (cardW + gridGapX), row = (y - gridTop + gridScroll) / gridRowH;
    if (x < gridX || col >= gridCols || y < barY + barH)
      return;
    if (row < 0)
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
  if (best != home.target) {
    home.achievementFocus = false;
    home.shotFocus = -1;
    home.target = best;
  } else if (best == count)
    openBrowser(BrowseMode::Games);
  else if (!home.details) {
    home.details = true;
    detailsAchievementKey.clear();
  } else {
    home.achievementFocus = false;
    home.shotFocus = -1;
    homeAction(Button::Cross);
  }
}

bool UI::homePointer(int state, int x, int y, uint32_t ms) {
  int count = int(homeList().size());
  if (state == 0) {
    home.pressed = true;
    home.dragging = false;
    home.pressX = home.lastX = x;
    home.pressY = home.lastY = y;
    home.dragFrom = home.position;
    home.velocity = 0;
    home.gridVelocity = 0;
    home.lastMove = ms;
    return false;
  }
  if (!home.pressed)
    return false;
  bool shelfDrag = home.panel == Home::NoPanel && home.viewing < 0 && !(home.grid() && home.details) &&
                   home.pressY >= barY + barH && home.pressY < hintsY;
  if (state == 1) {
    bool carousel = home.view == Home::Carousel;
    if (!home.dragging && shelfDrag && std::abs(x - home.pressX) > 10 && carousel)
      home.dragging = true;
    if (!home.dragging && shelfDrag && !carousel && std::abs(y - home.pressY) > 10)
      home.dragging = true;
    if (!home.dragging)
      return false;
    double dt = std::max(1u, ms - home.lastMove) / 1000.0;
    if (!carousel) {
      int dy = y - home.lastY;
      int &scroll = home.list() ? listScroll : gridScroll;
      int maxS = home.list() ? std::max(0, (count + 1 - listRows) * listRowH) : gridMaxScroll(count);
      scroll = std::max(0, std::min(maxS, scroll - dy));
      if (home.grid()) {
        double v = -double(dy) / dt;
        home.gridVelocity = home.gridVelocity * 0.4 + v * 0.6;
      }
      home.lastY = y;
      home.lastMove = ms;
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
    if (home.view == Home::Carousel) {
      int before = home.target;
      home.target = int(std::lround(home.position + home.velocity * 0.18));
      clampTarget(home, count);
      if (home.target != int(std::lround(home.position)) && home.target != before)
        emit(Sound::Move);
      home.details = home.details && home.target < count;
      if (home.target != before || !home.details) {
        home.achievementFocus = false;
        home.shotFocus = -1;
      }
    } else if (home.grid()) {
      if (std::fabs(home.gridVelocity) < 40.0 || ms - home.lastMove > 120)
        home.gridVelocity = 0;
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
         std::fabs((home.panel == Home::Filter ? 1.0 : 0.0) - home.sideAmount) > 0.002 ||
         (home.grid() && (std::fabs(home.gridVelocity) > 8.0 || home.gridLift < 0.995 || home.gridLiftFor != home.target));
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
  if (home.grid()) {
    if (home.gridLiftFor != home.target) {
      home.gridLiftFor = home.target;
      home.gridLift = 0.0;
    }
    if (home.gridLift < 1.0) {
      home.gridLift += (1.0 - home.gridLift) * (1 - std::exp(-seconds * 18.0));
      if (home.gridLift > 0.995)
        home.gridLift = 1.0;
    }
    if (std::fabs(home.gridVelocity) > 0.1) {
      int maxS = gridMaxScroll(int(homeList().size()));
      gridScroll = std::max(0, std::min(maxS, int(std::lround(gridScroll + home.gridVelocity * seconds))));
      home.gridVelocity *= std::exp(-seconds * 7.0);
      if (gridScroll == 0 || gridScroll == maxS || std::fabs(home.gridVelocity) < 8.0)
        home.gridVelocity = 0;
    }
  }
}
} // namespace hardrpg
