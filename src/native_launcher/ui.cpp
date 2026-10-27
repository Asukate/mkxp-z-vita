#include "home_layout.h"
#include "ui.h"
#include "i18n.h"
#include "release.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>
#include <atomic>
#include <cstdio>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#else
#include <thread>
#endif

namespace hardrpg {
namespace {
void move(int &selection, int &first, int count, int rows, int step) {
  if (!count) {
    selection = first = 0;
    return;
  }
  selection = (selection + step + count) % count;
  if (selection < first)
    first = selection;
  if (selection >= first + rows)
    first = selection - rows + 1;
}
const char *const displayKeys[] = {"fixedAspectRatio", "integerScalingActive",
                                   "smoothScaling"};
} // namespace

// One input, one sound: the state before and after an input decides what
// it did. Going deeper opens, coming back closes, a focus change moves,
// a changed value toggles.
struct InputFeedback {
  struct State {
    int depth = 0;
    std::vector<long> place, values;
    std::string message, missingRtp, display;
    bool launch = false;
  };
  UI &ui;
  Button button;
  State before;
  static State read(const UI &u) {
    State s;
    int browserDepth = 0;
    if (u.browser)
      browserDepth = int(std::count(u.browser->path.begin(), u.browser->path.end(), '/'));
    s.depth = (u.focus != Focus::Home) + (u.page != Page::Home) + u.home.details +
              (u.home.panel != Home::NoPanel) + u.gameOptions + bool(u.browser) + browserDepth +
              !u.errorReport.empty() + !u.releaseReport.empty() + u.controlsAdvanced + (u.controlsPad > 0) +
              u.controlPicking + u.controlsTesting + int(u.model.stack.size()) + u.welcome.active;
    s.place = {u.home.target, long(u.home.zone), u.home.toolbarFocus, u.home.topFocus, u.home.panelFocus,
               u.setting, u.gameSetting, u.rtpSelected, u.browserSelected, u.engineFilter, u.aboutScroll,
               u.errorScroll, u.releaseScroll, u.artRow, u.controlPickKey, u.removeStep ? long(u.removeStep) : 0,
               u.welcome.slide, u.welcome.languageFocus};
    s.values = {u.home.view, u.startView, u.home.sort, u.home.direction, u.home.favouritesOnly, u.home.unplayedOnly,
                long(u.home.hiddenEngines), u.blurAdult, u.automaticUpdates, long(u.favourites.size()),
                u.artPick[0], u.artPick[1], u.artPick[2], u.menuSounds, u.themeMusic, u.effectsVolume,
                u.musicVolume, long(u.query.size()), language()};
    s.message = u.message;
    s.missingRtp = u.missingRtp;
    s.launch = u.launchRequested;
    if (u.displayValues.is_object())
      s.display = u.displayValues.stringify();
    return s;
  }
  InputFeedback(UI &u, Button b) : ui(u), button(b) {
    if (ui.feedbackDepth++ == 0)
      before = read(ui);
  }
  ~InputFeedback() {
    if (--ui.feedbackDepth != 0)
      return;
    auto after = read(ui);
    if (after.launch && !before.launch)
      ui.emit(Sound::Launch);
    else if (!after.missingRtp.empty() && before.missingRtp.empty())
      ui.emit(Sound::Error);
    else if (after.depth > before.depth)
      ui.emit(Sound::Open);
    else if (after.depth < before.depth)
      ui.emit(button == Button::Circle ? Sound::Back : Sound::Close);
    else if (after.place != before.place)
      ui.emit(Sound::Move);
    else if (after.values != before.values || after.display != before.display)
      ui.emit(Sound::Toggle);
    else if (after.message != before.message && !after.message.empty() &&
             (button == Button::Cross || button == Button::Square || button == Button::Triangle))
      ui.emit(Sound::Select);
  }
};
void UI::emit(Sound s) const {
  if (sound && (menuSounds || s == Sound::Launch))
    sound(s);
}
std::string UI::themeFolder() const {
  // The list view plays no title music: nothing is read for it.
  if (!themeMusic || home.list() || browser || !(focus == Focus::Home || gameOptions))
    return "";
  auto *e = homeEntry();
  return e ? themeSource(*e) : "";
}
std::string UI::themeSource(const Entry &e) const {
  if (e.kind == Kind::Folder)
    return "";
  auto custom = customThemes.find(entryKey(e));
  if (custom != customThemes.end())
    return custom->second;
  if (!e.node.archive)
    return e.node.path();
  // A ZIP game plays from its unpacked copy, or from the ZIP itself.
  auto id = e.node.identity(model.paths);
  auto found = themeSourceCache.find(id);
  if (found != themeSourceCache.end())
    return found->second;
  auto unpacked = model.paths.at("cache/zip-" + key(id));
  return themeSourceCache[id] = directory(unpacked) ? unpacked : e.node.path();
}
std::vector<std::string> UI::themeSources() const {
  std::vector<std::string> out;
  if (!themeMusic || home.list())
    return out;
  for (auto &e : model.list) {
    auto s = themeSource(e);
    if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end())
      out.push_back(s);
  }
  return out;
}
std::vector<std::string> UI::themeNearby() const {
  std::vector<std::string> out;
  if (themeFolder().empty() || home.view != Home::Carousel)
    return out;
  auto list = homeList();
  for (int i : {home.target + 1, home.target - 1})
    if (i >= 0 && i < int(list.size()))
      out.push_back(themeSource(model.list[list[size_t(i)]]));
  return out;
}
std::vector<std::string> UI::rtpFolders() const {
  std::vector<std::string> out;
  try {
    auto paths = model.rtpPaths(); // kept: the loop reads into it
    for (auto &kv : paths.as_object())
      if (kv.second.is_string() && directory(kv.second.as_string()))
        out.push_back(kv.second.as_string());
  } catch (...) {
  }
  return out;
}
UI::UI(Paths paths, bool scan)
    : model(std::move(paths), scan),
      displayValues(std::initializer_list<Json::pair_type>{}),
      rtpChoices(std::initializer_list<Json::pair_type>{}) {
  try {auto text=readFile(model.paths.at("update-settings.json"),4096);
    if(!text.empty())automaticUpdates=json5pp::parse(text).as_object().at("automatic").as_boolean();
  }catch(...) {}
  if (scan) {
    errorReport = model.takeError();
    if (errorReport.empty())
      model.takeError(true);
    loadHome();
  }
}
void UI::scanLibrary() {
  model.refresh();
  errorReport = model.takeError();
  if (errorReport.empty())
    model.takeError(true);
  loadHome();
}
void UI::launchFailed(int code, const std::string &executable) {
  launchRequested = false;
  launchFormat = 0;
  launchingName.clear();
  preparationLabel.clear();
  preparationFraction = -1;
  char hexadecimal[16];
  std::snprintf(hexadecimal, sizeof(hexadecimal), "0x%08x", unsigned(code));
  message = trf("Could not start game (LoadExec {}, {}; {}).", {std::to_string(code), hexadecimal, executable});
  emit(Sound::Error);
}
struct RtpCheck {
  unsigned generation;
  int pack;
  std::string result, error;
  std::atomic<bool> done{false};
  Model snapshot;
#ifdef __vita__
  SceUID worker = -1;
  static int run(SceSize, void *argument) {
    auto self = *static_cast<RtpCheck **>(argument);
    self->check();
    sceKernelExitThread(0);
    return 0;
  }
#else
  std::thread worker;
#endif
  void check() {
    try { result = snapshot.rtpStatus(packs[pack]); }
    catch (const std::exception &e) { result = "Unavailable"; error = e.what(); }
    catch (...) { result = "Unavailable"; error = tr("RTP check failed."); }
    done.store(true, std::memory_order_release);
  }
  RtpCheck(Model copy, unsigned gen, int index)
      : generation(gen), pack(index), snapshot(std::move(copy)) {
#ifdef __vita__
    worker = sceKernelCreateThread("hardrpg_rtp_check", run,
                                  0x10000100, 128 * 1024, 0, 0, nullptr);
    auto self = this;
    if (worker < 0 || sceKernelStartThread(worker, sizeof(self), &self) < 0) {
      if (worker >= 0) sceKernelDeleteThread(worker);
      worker = -1;
      throw std::runtime_error(tr("Cannot start RTP location check."));
    }
#else
    worker = std::thread([this] { check(); });
#endif
  }
  ~RtpCheck() {
#ifdef __vita__
    if (worker >= 0) {
      sceKernelWaitThreadEnd(worker, nullptr, nullptr);
      sceKernelDeleteThread(worker);
    }
#else
    if (worker.joinable()) worker.join();
#endif
  }
};
void UI::refreshRtp() {
  ++rtpGeneration;
  rtpChoices = model.rtpPaths();
  rtpStatuses.assign(rtpPackCount, "Checking...");
  rtpPending = 0;
}
void UI::offerRelease(const ReleaseNotice &notice) {
  if(notice.tag.empty())return;
  releaseNotice=notice;
  if(errorReport.empty() && missingRtp.empty() && !browser && !gameOptions && !welcome.active)showRelease();
}
void UI::showRelease() {
  if(releaseNotice.tag.empty())return;
  releaseReport.clear();releaseScroll=0;
  releaseReport.push_back(trf("Installed: {}", {HARDRPG_VERSION_TAG}));
  releaseReport.push_back(trf("Available: {}", {releaseNotice.tag}));
  releaseReport.push_back(releaseNotice.name);
  releaseReport.push_back("");
  std::stringstream body(releaseNotice.body);std::string line;
  while(std::getline(body,line)) {
    if(line.empty())releaseReport.push_back("");
    while(!line.empty()) {
      size_t n=std::min(size_t(70),line.size());
      while(n<line.size() && (uint8_t(line[n])&0xc0)==0x80)++n;
      releaseReport.push_back(line.substr(0,n));line.erase(0,n);
    }
  }
  releaseReport.push_back("");releaseReport.push_back(tr("Download on your PC:"));
  releaseReport.push_back(releaseNotice.url);
}
bool UI::service() {
  if (rtpCheck) {
    if (!rtpCheck->done.load()) return false;
    if (page == Page::Rtp && rtpCheck->generation == rtpGeneration) {
      rtpStatuses[rtpCheck->pack] = rtpCheck->result;
      if (!rtpCheck->error.empty()) message = rtpCheck->error;
      rtpPending = rtpCheck->pack + 1;
      if (rtpPending == rtpPackCount) rtpPending = -1;
    }
    rtpCheck.reset();
    return true;
  }
  if (page != Page::Rtp) { rtpPending = -1; return false; }
  if (rtpPending < 0) return false;
  // The worker owns a snapshot; it never mutates the live model or UI.
  try {
    rtpCheck = std::make_shared<RtpCheck>(model, rtpGeneration, rtpPending);
  } catch (const std::exception &e) {
    rtpStatuses[rtpPending] = "Unavailable";
    message = e.what();
    if (++rtpPending == rtpPackCount) rtpPending = -1;
    return true;
  }
  return false;
}
std::vector<size_t> UI::visible() const {
  std::vector<size_t> out;
  auto q = lower(query);
  for (size_t i = 0; i < model.list.size(); ++i) {
    auto &e = model.list[i];
    if (!q.empty() && lower(e.name).find(q) == std::string::npos)
      continue;
    // Collections stay navigable; engines are filtered once a game is
    // identified.
    if (engineFilter && e.version && e.version != engineFilter)
      continue;
    if (engineFilter && e.kind == Kind::Game && e.version != engineFilter)
      continue;
    out.push_back(i);
  }
  return out;
}
void UI::search(const std::string &text) {
  query = text;
  if (query.size() > 512)
    query.resize(512);
}
void UI::scroll(int rows) {
  if (homeActive())
    return;
  if (gameOptions && page != Page::Controls) {
    if (browser || !errorReport.empty() || !releaseReport.empty() || !missingRtp.empty()) return;
    int count = optionRows();
    setting = std::max(0, std::min(setting + rows, std::max(0, count - 1)));
    settingFirst = std::max(0, setting - 6);
    return;
  }
  if(page==Page::Controls) {if(!controlsTesting && !controlPicking) {setting=std::max(0,std::min(controlsCount()-1,setting+rows));settingFirst=std::max(0,setting-6);} return;}
  if (controlsTesting || browser || !errorReport.empty() || !releaseReport.empty() || !missingRtp.empty())
    return;
  if (focus == Focus::About) {
    aboutScroll = std::max(
        0, std::min(aboutScroll + rows, std::max(0, int(sizeof(releaseNotes) /
                                                        sizeof(*releaseNotes)) -
                                                        9)));
  } else if (focus == Focus::Settings) {
    if (page == Page::Rtp) {
      rtpSelected = std::max(0, std::min(rtpSelected + rows, rtpPackCount - 1));
      return;
    }
    int count = page == Page::Errors    ? errors.size()
                : page == Page::Folders ? 1 + gameFolders.size()
                : page == Page::Home    ? settingsRows()
               : page == Page::Keys    ? keysRows
                : page == Page::Sound   ? soundRows
                : page == Page::Metrics ? 3
                                        : 3;
    setting = std::max(0, std::min(setting + rows, std::max(0, count - 1)));
    settingFirst = std::max(0, std::min(settingFirst, setting));
    if (setting >= settingFirst + 7)
      settingFirst = setting - 6;
  }
}
void UI::touch(int x, int y) {
  InputFeedback feedback(*this, Button::Cross);
  if (welcome.active) {
    welcomeTap(x, y);
    return;
  }
  if (homeActive()) {
    try {
      homeTap(x, y);
    } catch (const std::exception &e) {
      message = e.what();
    }
    return;
  }
  if(page==Page::Controls && controlPicking) {
    for (int i = 0; i < controlPickCount; ++i) {
      int kx, ky, kw, kh;
      controlPickRect(i, kx, ky, kw, kh);
      if (x >= kx && x < kx + kw && y >= ky && y < ky + kh) {
        // First tap selects, a second tap on the same key assigns it.
        if (controlPickKey == i)
          assignControl(controlPicks[i].value);
        else
          controlPickKey = i;
        break;
      }
    }
    return;
  }
  if(page==Page::Controls && controlsPad && !controlsTesting) {
    for (int row = 0; row < controlsCount(); ++row) {
      int rx, ry, rw, rh;
      padRowRect(row, rx, ry, rw, rh);
      if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
        bool again = setting == row;
        setting = row;
        if (again) {
          if (controlTarget(row))
            openControlPicker();
          else
            changeControl(1);
        }
        break;
      }
    }
    return;
  }
  sheetTouch(x, y);
}
void UI::sheetTouch(int x, int y) {
  using namespace homeLayout;
  // The hint bar's buttons act like the buttons they show.
  if (y >= hintsY) {
    for (auto &h : hintHit)
      if (inside(x, y, h.x, hintsY, h.w, 544 - hintsY)) {
        action(h.button);
        return;
      }
    return;
  }
  bool busy = browser || !errorReport.empty() || !releaseReport.empty() || !missingRtp.empty() || gameOptions ||
              page == Page::Controls;
  if (y < topHeight) {
    // Settings and About from their icons; the brand goes home.
    if (busy)
      return;
    if (inside(x, y, gearX, topIconY, topIcon, topIcon)) {
      focus = Focus::Settings;
      openPage(Page::Home);
    } else if (inside(x, y, aboutX, topIconY, topIcon, topIcon)) {
      focus = Focus::About;
    } else if (x < 300) {
      focus = Focus::Home;
      message.clear();
    }
    return;
  }
  // The row under the finger, or -1.
  auto rowAt = [&](int rowH, int visible) {
    if (x < sheetX || x >= sheetX + sheetW || y < sheetTop)
      return -1;
    int r = (y - sheetTop) / rowH;
    return r < visible ? r : -1;
  };
  // A first tap moves the focus to a row; tapping the focused row opens it.
  auto pickRow = [&](int &target, int index, int count) {
    if (index < 0 || index >= count)
      return;
    bool again = target == index;
    target = index;
    if (again)
      action(Button::Cross);
  };
  if (controlsMap()) {
    for (int row = 0; row <= 18; ++row) {
      int rx, ry, rw, rh;
      if (controlsRect(row, rx, ry, rw, rh) && inside(x, y, rx, ry, rw, rh)) {
        bool again = setting == row;
        setting = row;
        if (again)
          action(Button::Cross);
        return;
      }
    }
    return;
  }
  if (page == Page::Controls) {
    if (controlsTesting)
      return;
    int r = rowAt(sheetRowH, sheetRows);
    if (r < 0 || settingFirst + r >= controlsCount())
      return;
    int row = settingFirst + r;
    bool again = setting == row;
    setting = row;
    if (again) {
      if (controlTarget(row))
        openControlPicker();
      else
        changeControl(1);
    }
    return;
  }
  if (!missingRtp.empty()) {
    int r = rowAt(sheetRowH, 2);
    if (r >= 0)
      action(r == 0 ? Button::Cross : Button::Circle);
    return;
  }
  if (browser) {
    int r = rowAt(browserRowH, browserRows);
    if (r >= 0)
      pickRow(browserSelected, browserFirst + r, int(browser->list.size()));
    return;
  }
  if (!errorReport.empty() || !releaseReport.empty() || focus == Focus::About)
    return;
  bool rtp = page == Page::Rtp;
  int r = rowAt(rtp ? rtpRowH : sheetRowH, rtp ? rtpPackCount : sheetRows);
  if (r < 0)
    return;
  if (gameOptions) {
    if (page != Page::GameInfo)
      pickRow(rtp ? rtpSelected : setting, (rtp ? 0 : settingFirst) + r, optionRows());
    return;
  }
  if (focus == Focus::Settings) {
    int count = page == Page::Home      ? settingsRows()
                : page == Page::Keys    ? keysRows
                : page == Page::Sound   ? soundRows
                : page == Page::Updates ? 3
                : page == Page::Display ? 3
                : page == Page::Metrics ? 3
                : page == Page::Rtp     ? rtpPackCount
                : page == Page::Folders ? 1 + int(gameFolders.size())
                                        : int(errors.size());
    pickRow(rtp ? rtpSelected : setting, (rtp ? 0 : settingFirst) + r, count);
  }
}

void UI::openControls() {
  auto entry = gameEntry;
  auto profile = model.controls(entry);
  controlsEntry = entry;
  controls = profile;
  page = Page::Controls;
  setting = settingFirst = 0;
  focus = Focus::Settings;
  controlsTesting = false;
  controlPicking = false;
  controlsAdvanced = false;
  controlsPad = 0;
  controlsTestState.reset();
  message.clear();
}
namespace {
std::string comingSoonMessage(int format) {
  return trf("RPG Maker {} game detected. HardRPG cannot run MV or MZ games yet: support is coming in a future version.",
             {format == formatMZ ? "MZ" : "MV"});
}
} // namespace
void UI::openGameMenu(size_t index) {
  if (index >= model.list.size())
    return;
  auto source = model.list[index];
  if (comingSoon(source.version))
    throw std::runtime_error(comingSoonMessage(source.version));
  Entry entry;
  if (!configurableGame(source, entry))
    throw std::runtime_error(tr("Open this collection and highlight a game first."));
  auto values = model.gameSettings(entry);
  gameMenuSource = source;
  gameEntry = entry;
  if (source.kind == Kind::Game ||
      source.name !=
          source.node.path().substr(source.node.path().find_last_of('/') + 1))
    gameEntry.name = source.name;
  displayValues = values;
  {
    auto r = model.removal(entry);
    canFreeZip = !r.zip.empty() && !r.unpacked.empty();
  }
  gameOptions = true;
  gameSetting = setting = settingFirst = 0;
  page = Page::Game;
  focus = Focus::Settings;
  message.clear();
}
void UI::openInGame(const Entry &entry) {
  gameEntry = gameMenuSource = entry;
  displayValues = model.gameSettings(entry);
  canFreeZip = false;
  gameOptions = inGame = true;
  inGameExit = Staying;
  inGameChanged = false;
  gameSetting = setting = settingFirst = 0;
  page = Page::Game;
  focus = Focus::Settings;
  message.clear();
}
int UI::optionRows() const {
  return page == Page::Game       ? gameOptionsCount()
         : page == Page::Metrics  ? 3
         : page == Page::Rtp      ? rtpPackCount
         : page == Page::GameInfo ? 0
         : page == Page::Remove   ? 2
         : page == Page::Rescue   ? 3
                                  : 3;
}
void UI::openRemove() {
  removeInfo = model.removal(gameEntry);
  removeQuestions.clear();
  bool source = !removeInfo.folder.empty() || !removeInfo.zip.empty();
  // A game whose ZIP is gone exists only as its unpacked copy: that copy is
  // its game files.
  if (source || !removeInfo.unpacked.empty())
    removeQuestions.push_back(DeleteFiles);
  if (source && !removeInfo.unpacked.empty())
    removeQuestions.push_back(DeleteUnpacked);
  removeStep = 0;
  removeFiles = removeUnpacked = false;
  if (removeQuestions.empty()) {
    finishRemove();
    return;
  }
  page = Page::Remove;
  setting = settingFirst = 0;
  message.clear();
}
std::string UI::removePath(RemoveQuestion q) const {
  if (q == DeleteUnpacked)
    return removeInfo.unpacked;
  if (q == DeleteZipOnly)
    return removeInfo.zip;
  return !removeInfo.folder.empty() ? removeInfo.folder
         : !removeInfo.zip.empty()  ? removeInfo.zip
                                    : removeInfo.unpacked;
}
void UI::openFreeZip() {
  removeInfo = model.removal(gameEntry);
  removeQuestions = {DeleteZipOnly};
  removeStep = 0;
  removeFiles = removeUnpacked = false;
  page = Page::Remove;
  setting = settingFirst = 0;
  message.clear();
}
void UI::answerRemove(bool yes) {
  auto q = removeQuestions[removeStep];
  if (q == DeleteZipOnly) {
    if (!yes) {
      page = Page::Game;
      setting = gameSetting;
      settingFirst = std::max(0, setting - 6);
      return;
    }
    auto name = gameEntry.name;
    model.deleteZip(gameEntry);
    gameOptions = false;
    page = Page::Home;
    focus = Focus::Home;
    message = trf("ZIP deleted. {} plays from its unpacked copy.", {name});
    return;
  }
  bool source = !removeInfo.folder.empty() || !removeInfo.zip.empty();
  if (q == DeleteFiles && source)
    removeFiles = yes;
  else
    removeUnpacked = yes;
  if (++removeStep < removeQuestions.size()) {
    setting = 0; // No is the default
    return;
  }
  finishRemove();
}
void UI::finishRemove() {
  auto name = gameEntry.name;
  model.remove(gameEntry, removeFiles, removeUnpacked);
  gameOptions = false;
  page = Page::Home;
  focus = Focus::Home;
  // The neighbour takes the removed game's place on home.
  home.target = std::max(0, std::min(home.target, int(homeList().size()) - 1));
  message = removeFiles || removeUnpacked
                ? trf("{} removed and its files deleted.", {name})
                : trf("{} removed from the list. Files kept; <start> adds it again.", {name});
}
void UI::renameGame(const Entry &entry) {
  if (!editName)
    throw std::runtime_error(tr("Game name keyboard is unavailable."));
  std::string name;
  if (!editName(entry.name, name))
    return;
  auto id = entry.node.identity(model.paths);
  model.setDisplayName(entry, name);
  if (gameOptions) {
    Entry original;
    configurableGame(entry, original);
    gameEntry.name = original.name;
    for (const auto &candidate : model.list)
      if (candidate.node.identity(model.paths) == id) {
        gameMenuSource = candidate;
        if (candidate.kind == Kind::Game || !name.empty())
          gameEntry.name = candidate.name;
        break;
      }
  }
  // Home keeps the renamed game focused; when the search no longer shows
  // it, its neighbour takes its place.
  auto list = homeList();
  home.target = std::max(0, std::min(home.target, int(list.size()) - 1));
  for (size_t i = 0; i < list.size(); ++i)
    if (model.list[list[i]].node.identity(model.paths) == id)
      home.target = int(i);
  message = tr(name.empty() ? "Original game name restored." : "Display name saved.");
}
void UI::gameAction(Button b) {
  if (page == Page::Rtp && b == Button::Triangle) {
    model.clearRtpCache(); refreshRtp(); message = tr("Checking RTP locations..."); return;
  }
  if (controlsTesting) {
    if (b == Button::Circle) {
      controlsTesting = false;
      controlsTestState.reset();
    }
    return;
  }
  if (controlPicking) {
    pickControlKey(b);
    return;
  }
  if (page == Page::Controls && (controlsAdvanced || controlsPad) && b == Button::Circle) {
    setting = controlsPad ? 13 + controlsPad : 16;
    controlsAdvanced = false;
    controlsPad = 0;
    settingFirst = std::max(0, setting - 6);
    message.clear();
    return;
  }
  if (b == Button::Circle && inGame && page == Page::Game) {
    inGameExit = Resume;
    return;
  }
  if (b == Button::Circle && page == Page::Rescue && rescueFromHome) {
    gameOptions = false;
    page = Page::Home;
    focus = Focus::Home;
    message.clear();
    return;
  }
  if (b == Button::Circle) {
    if (page == Page::Game) {
      gameOptions = false;
      page = Page::Home;
      focus = Focus::Home;
    } else {
      page = Page::Game;
      setting = gameSetting;
      settingFirst = std::max(0, setting - 6);
    }
    message.clear();
    return;
  }
  if (controlsMap() && (b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right)) {
    setting = controlsNeighbor(setting, b);
    message.clear();
    return;
  }
  if (b == Button::Up || b == Button::Down) {
    int step = b == Button::Up ? -1 : 1;
    if (page == Page::Rtp)
      rtpSelected = (rtpSelected + step + rtpPackCount) % rtpPackCount;
    else {
      int count = page == Page::Controls ? controlsCount() : optionRows();
      move(setting, settingFirst, count, 7, step);
    }
    message.clear();
    return;
  }
  if (page == Page::Game && b == Button::Cross && inGame) {
    gameSetting = setting;
    switch (setting) {
    case 0:
      inGameExit = Resume;
      break;
    case 1:
      openControls();
      break;
    case 2:
      openPage(Page::Display);
      break;
    case 3:
      openPage(Page::Metrics);
      break;
    case 4:
      inGameExit = BackToLibrary;
      break;
    }
    return;
  }
  if (page == Page::Game && b == Button::Cross) {
    gameSetting = setting;
    switch (setting) {
    case 0:
      openControls();
      break;
    case 1:
      openPage(Page::Display);
      break;
    case 2:
      openPage(Page::Audio);
      break;
    case 3:
      openPage(Page::Metrics);
      break;
    case 4:
      openPage(Page::Rtp);
      rtpSelected = std::max(0, gameEntry.version - 1);
      break;
    case 5:
      renameGame(gameMenuSource);
      break;
    case 6:
      openPage(Page::GameInfo);
      break;
    case 8:
      openRemove();
      break;
    case 9:
      openFreeZip();
      break;
    case 7:
      if (offerRescue(gameEntry, gameMenuSource, false))
        break;
      startGame(gameEntry);
      break;
    }
    return;
  }
  if (page == Page::Remove && b == Button::Cross) {
    answerRemove(setting == 1);
    return;
  }
  if (page == Page::Rescue && b == Button::Cross) {
    answerRescue();
    return;
  }
  if (page == Page::Controls && b == Button::Cross && controlTarget(setting)) {
    openControlPicker();
    return;
  }
  if (page == Page::Controls && controlsPad && setting >= 1 &&
      (b == Button::Left || b == Button::Right)) {
    // Zones side by side are neighbouring rows.
    if (padMask() == 3 || padMask() == 15)
      setting += setting % 2 ? 1 : -1;
    return;
  }
  if (page == Page::Controls &&
      (b == Button::Cross || b == Button::Left || b == Button::Right)) {
    changeControl(b == Button::Left ? -1 : 1);
    return;
  }
  bool reset = b == Button::Square;
  if (reset || b == Button::Cross || b == Button::Left || b == Button::Right) {
    int step = b == Button::Left ? -1 : 1;
    if (page == Page::Display)
      changeDisplay(step, reset);
    else if (page == Page::Metrics)
      changeMetrics(step, reset);
    else if (page == Page::Audio)
      changeAudio(reset);
    else if (page == Page::Rtp) {
      if (reset) {
        model.setRtp(packs[rtpSelected]);
        refreshRtp();
        message = tr("Automatic RTP location restored.");
      } else if (b == Button::Cross)
        openBrowser(BrowseMode::Rtp);
    }
    inGameChanged = inGameChanged || page == Page::Display || page == Page::Metrics;
  }
}
namespace {
// Buttons on the drawing (Tokyoship's illustration, 1800 px wide), in
// controlSources order: Cross, Circle, Square, Triangle, Select, Start, Up,
// Down, Left, Right, L, R.
const int mapButtons[12][2] = {{1627, 350}, {1707, 273}, {1547, 273}, {1627, 197}, {1550, 644}, {1648, 644},
                               {171, 208},  {171, 342},  {104, 274},  {238, 274},  {440, 34},   {1660, 34}};
// Label columns, top to bottom, as button indexes.
const int mapLeft[] = {10, 6, 8, 9, 7};
const int mapRight[] = {11, 3, 1, 2, 0, 4, 5};
const int mapPills[] = {0, 13, 15, 16, 17, 18};
} // namespace
bool UI::controlsRect(int row, int &x, int &y, int &w, int &h) {
  const double k = mapArtW / 1800.0;
  if (row == 14) { // the screen in the drawing
    x = mapArtX + int(356 * k + .5), y = mapArtY + int(66 * k + .5);
    w = int(1087 * k + .5), h = int(607 * k + .5);
    return true;
  }
  for (int i = 0; i < 6; ++i)
    if (mapPills[i] == row) {
      x = 16 + i * 156, y = 440, w = 150, h = 46;
      return true;
    }
  if (row < 1 || row > 12)
    return false;
  int button = row - 1;
  for (int i = 0; i < 5; ++i)
    if (mapLeft[i] == button) {
      x = 16, y = 150 + i * 52, w = 190, h = 36;
      return true;
    }
  for (int i = 0; i < 7; ++i)
    if (mapRight[i] == button) {
      x = 754, y = 150 + i * 41, w = 190, h = 36;
      return true;
    }
  return false;
}
bool UI::controlsAnchor(int row, int &x, int &y) {
  if (row < 1 || row > 12)
    return false;
  const double k = mapArtW / 1800.0;
  x = mapArtX + int(mapButtons[row - 1][0] * k + .5);
  y = mapArtY + int(mapButtons[row - 1][1] * k + .5);
  return true;
}
int UI::controlsNeighbor(int row, Button b) {
  int x, y, w, h;
  if (!controlsRect(row, x, y, w, h))
    return row;
  double cx = x + w / 2.0, cy = y + h / 2.0;
  bool vertical = b == Button::Up || b == Button::Down;
  int best = row;
  double bestScore = 1e18;
  for (int other = 0; other <= 18; ++other) {
    int ox, oy, ow, oh;
    if (other == row || !controlsRect(other, ox, oy, ow, oh))
      continue;
    double dx = ox + ow / 2.0 - cx, dy = oy + oh / 2.0 - cy;
    // Its centre must be that way.
    double toward = b == Button::Up ? -dy : b == Button::Down ? dy : b == Button::Left ? -dx : dx;
    if (toward <= 1)
      continue;
    // The gap to it that way, and its gap to the side (none when the two
    // overlap side to side): the nearest one in line wins.
    auto gap = [](int a0, int a1, int b0, int b1) { return std::max(0, std::max(b0 - a1, a0 - b1)); };
    double ahead = vertical ? gap(y, y + h, oy, oy + oh) : gap(x, x + w, ox, ox + ow);
    double side = vertical ? gap(x, x + w, ox, ox + ow) : gap(y, y + h, oy, oy + oh);
    double score = ahead + side * 4 + toward * 0.01;
    if (score < bestScore) {
      bestScore = score;
      best = other;
    }
  }
  return best;
}
// The controls menu has two lists. The main one is what most games need:
// the switch, one row per button, then the tester and reset. Touch zones,
// the pointer and stick tuning are on a second list behind one row.
int UI::controlsCount() const {
  if (controlsPad) {
    int rows = 1;
    for (unsigned mask = padMask(); mask; mask >>= 1)
      rows += mask & 1;
    return rows;
  }
  return controlsAdvanced ? 4 : 19;
}
int *UI::controlTarget(int row) {
  if (controlsPad) {
    int zone = padZone(row);
    return zone >= 0 ? controls.zone(controlsPad == 2, zone) : nullptr;
  }
  if (!controlsAdvanced)
    return row >= 1 && row <= 12 ? &controls.buttons[row - 1] : nullptr;
  return nullptr;
}
// The picker offers the RPG Maker actions and a keyboard, starting on what
// the input does now.
void UI::openControlPicker() {
  controlPicking = true;
  int current = controlPickIndex(*controlTarget(setting));
  controlPickKey = current >= 0 ? current : 0;
  message.clear();
}
void UI::assignControl(int value) {
  if (int *target = controlTarget(setting)) {
    *target = value;
    // Choosing a mapping is asking for custom controls.
    controls.enabled = true;
    model.saveControls(controlsEntry, controls);
    inGameChanged = true;
    message = trf(inGame ? "{} now does {}. Custom controls are on and apply now."
                         : "{} now does {}. Custom controls are on; applies on next launch.",
                  {controlLabel(setting), controlText(value)});
  }
  controlPicking = false;
}
void UI::pickControlKey(Button b) {
  const ControlPick &at = controlPicks[controlPickKey];
  if (b == Button::Circle) {
    controlPicking = false;
    return;
  }
  if (b == Button::Cross) {
    assignControl(at.value);
    return;
  }
  if (b == Button::Left || b == Button::Right) {
    // Neighbours in a row are neighbours in the table; wrap at the ends.
    int step = b == Button::Left ? -1 : 1, next = controlPickKey + step;
    if (next < 0 || next >= controlPickCount || controlPicks[next].row != at.row) {
      next = controlPickKey;
      while (next - step >= 0 && next - step < controlPickCount &&
             controlPicks[next - step].row == at.row)
        next -= step;
    }
    controlPickKey = next;
    return;
  }
  if (b == Button::Up || b == Button::Down) {
    int row = (at.row + (b == Button::Up ? -1 : 1) + controlPickRows) % controlPickRows;
    float centre = at.x + at.w / 2, best = 1e9f;
    for (int i = 0; i < controlPickCount; ++i)
      if (controlPicks[i].row == row) {
        float distance = std::abs(controlPicks[i].x + controlPicks[i].w / 2 - centre);
        if (distance < best) {
          best = distance;
          controlPickKey = i;
        }
      }
  }
}
static const char *const frontModeNames[] = {
    "Off", "Two buttons on the game picture", "Mouse pointer", "D-pad and button on the game picture",
    "Whole screen", "Screen halves, left and right", "Screen corners"};
static const char *const rearModeNames[] = {
    "Off", "Two upper zones", "Trackpad (hold <l>)", "Four corners",
    "Whole pad", "Left / right halves"};
std::string UI::zoneName(int row) const {
  unsigned mask = padMask();
  int zone = padZone(row);
  const char *names[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
  return tr(mask == 15 ? names[zone]
            : mask == 3 ? (zone ? "Right" : "Left")
            : controlsPad == 1 && controls.front == 3 ? "Button"
            : controlsPad == 2 ? "Whole pad" : "Whole screen");
}
std::string UI::controlLabel(int row) const {
  if (controlsPad) {
    if (row == 0)
      return tr(controlsPad == 2 ? "Rear pad" : "Front screen");
    return trf(controlsPad == 2 ? "Rear pad: {}" : "Front screen: {}", {zoneName(row)});
  }
  if (!controlsAdvanced) {
    if (row >= 1 && row <= 12)
      return std::string(buttonIcon(row - 1)) + " " + trf("{} button", {buttonText(row - 1)});
    return tr(row == 0 ? "Custom controls" : row == 13 ? "Tap to walk"
              : row == 14 ? "Front touch screen"
              : row == 15 ? "Rear touch pad"
              : row == 16 ? "Sticks and pointer"
              : row == 17 ? "Test inputs" : "Reset everything");
  }
  const char *labels[] = {"Stick deadzone", "Right stick moves pointer",
                          "Pointer speed", "<square> Square toggles its action"};
  return row >= 0 && row < 4 ? tr(labels[row]) : "";
}
std::string UI::controlValue(int row) const {
  if (controlsPad) {
    if (row == 0)
      return tr(controlsPad == 2 ? rearModeNames[controls.rear] : frontModeNames[controls.front]);
    int zone = padZone(row);
    return zone >= 0 ? controlText(controls.zone(controlsPad == 2, zone)) : "";
  }
  if (!controlsAdvanced) {
    if (row >= 1 && row <= 12)
      return controlText(controls.buttons[row - 1]);
    return tr(row == 0 ? (controls.enabled ? "On" : "Off (game's own controls)")
              : row == 13 ? (controls.tapToWalk ? "On (tap the map to walk there)" : "Off")
              : row == 14 ? frontModeNames[controls.front]
              : row == 15 ? rearModeNames[controls.rear]
              : row == 16 || row == 17 ? "Open" : "Back to defaults, off");
  }
  switch (row) {
  case 0:
    return std::to_string(controls.deadzone);
  case 1:
    return tr(controls.rightPointer ? "On" : "Off");
  case 2:
    return std::to_string(controls.speed);
  default:
    return tr(controls.toggleRun ? "On" : "Off");
  }
}
void UI::changeControl(int step) {
  if (controlTarget(setting))
    return; // Cross opens the picker; Left and Right change nothing.
  message.clear();
  if (controlsPad) {
    int &mode = controlsPad == 2 ? controls.rear : controls.front;
    int count = controlsPad == 2 ? rearModes : frontModes;
    mode = (mode + step + count) % count;
    if (mode)
      controls.enabled = true;
    // Tap to walk needs the front screen as the pointer; touch buttons and
    // zones take the screen instead.
    if (controlsPad == 1 && mode != 0 && mode != 2 && controls.tapToWalk) {
      controls.tapToWalk = false;
      message = tr("Tap to walk turned off: the screen is touch zones now.");
    }
  } else if (!controlsAdvanced) {
    if (setting >= 14 && setting <= 16) {
      if (setting == 16)
        controlsAdvanced = true;
      else
        controlsPad = setting - 13;
      setting = settingFirst = 0;
      return;
    }
    if (setting == 17) {
      controlsTesting = true;
      controlsTestState.reset();
      return;
    }
    if (setting == 0)
      controls.enabled = !controls.enabled;
    else if (setting == 13) {
      controls.tapToWalk = !controls.tapToWalk;
      if (controls.tapToWalk && controls.front != 0 && controls.front != 2) {
        controls.front = 0;
        message = tr("Tap to walk on; front touch zones turned off. Next launch.");
      }
    } else
      controls = {};
  } else {
    switch (setting) {
    case 0:
      controls.deadzone = std::max(8, std::min(80, controls.deadzone + step * 4));
      break;
    case 1:
      controls.rightPointer = !controls.rightPointer;
      break;
    case 2:
      controls.speed = std::max(1, std::min(10, controls.speed + step));
      break;
    default:
      controls.toggleRun = !controls.toggleRun;
      break;
    }
  }
  model.saveControls(controlsEntry,controls);inGameChanged=true;
  if(message.empty())
    message=setting==13 && !controlsAdvanced && !controlsPad && controls.tapToWalk
            ?tr("Tap to walk on: works in games with standard tile movement. Next launch.")
            :trf(inGame ? "Saved for {}. Applied now." : "Saved for {}. Applies on next launch.",{controlsEntry.name});
}
void UI::refreshFolders() { gameFolders = model.searchFolders(); }
// The key rows, in the order the Game info and artwork page lists them.
static const char *const keyFiles[] = {"steamgriddb-key.txt", "igdb-client-id.txt", "igdb-client-secret.txt",
                                       "rawg-key.txt"};
static const char *const keyTitles[] = {"SteamGridDB API key", "IGDB (Twitch) client ID", "IGDB (Twitch) client secret",
                                        "RAWG API key"};
std::string UI::keyFile(int index) const { return model.paths.at(keyFiles[index]); }
std::string UI::readKey(int index) const {
  std::ifstream in(keyFile(index));
  std::string key;
  std::getline(in, key);
  key.erase(std::remove_if(key.begin(), key.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }),
            key.end());
  return key;
}
void UI::editKey(int index) {
  if (!enterText)
    return;
  std::string out;
  if (!enterText(tr(keyTitles[index]), readKey(index), out))
    return;
  out.erase(std::remove_if(out.begin(), out.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }),
            out.end());
  if (out.empty()) {
    std::remove(keyFile(index).c_str());
    message = tr("Key removed.");
    return;
  }
  std::ofstream(keyFile(index)) << out << "\n";
  message = tr("Key saved. Artwork downloads use it from now on.");
}
void UI::openPage(Page next) {
  if (next == Page::Display || next == Page::Metrics || next == Page::Audio || next == Page::GameInfo)
    displayValues = gameOptions ? model.gameSettings(gameEntry) : model.display();
  if (next == Page::Rtp) {
    if (rtpStatuses.empty() || rtpPending >= 0 ||
        std::find(rtpStatuses.begin(), rtpStatuses.end(), "Checking...") != rtpStatuses.end()) refreshRtp();
    else rtpChoices = model.rtpPaths();
  }
  if (next == Page::Folders)
    refreshFolders();
  if (next == Page::Errors)
    errors = model.errorHistory();
  page = next;
  setting = settingFirst = 0;
  message.clear();
}
void UI::openBrowser(BrowseMode mode) {
  browseMode = mode;
  browser.reset(new Browser(model.paths, mode == BrowseMode::Rtp));
  browserSelected = browserFirst = 0;
  message.clear();
}
void UI::changeDisplay(int step, bool reset) {
  const auto k = displayKeys[setting];
  bool defaultBool = setting == 0;
  auto i = displayValues.as_object().find(k);
  Json v = setting == 2 ? Json(Json::integer_type(filterDefault)) : Json(defaultBool);
  if (i != displayValues.as_object().end())
    v = i->second;
  if (reset)
    v = setting == 2 ? Json(Json::integer_type(filterDefault)) : Json(defaultBool);
  else if (setting == 2) {
    int n = filterIndex(v);
    v = Json::integer_type(filterValues[(n < 0 ? 1 : n + step + filterCount) % filterCount]);
  } else
    v = v.is_boolean() ? !v.as_boolean() : !defaultBool;
  if (gameOptions) model.changeGameSetting(gameEntry, k, v, reset);
  else model.changeDisplay(k, v);
  displayValues = gameOptions ? model.gameSettings(gameEntry) : model.display();
  message = gameOptions
                ? trf(inGame ? "Saved for {}. Applied now." : "Saved for {}. Applies on next launch.", {gameEntry.name})
                : tr("Display settings saved for the next game launch.");
}
void UI::changeMetrics(int step, bool reset) {
  if (setting == 1 || setting == 2) {
    // Fast forward (L+R in game) and crash rescue: on unless switched off.
    const char *key = setting == 1 ? "fastForward" : "crashRescue";
    auto found = displayValues.as_object().find(key);
    bool on = found == displayValues.as_object().end() || !found->second.is_boolean() ||
              found->second.as_boolean();
    Json value(reset ? true : !on);
    if (gameOptions)
      model.changeGameSetting(gameEntry, key, value, reset);
    else
      model.changeDisplay(key, value);
    displayValues = gameOptions ? model.gameSettings(gameEntry) : model.display();
    message = gameOptions ? trf(inGame ? "Saved for {}. Applied now." : "Saved for {}. Applies on next launch.", {gameEntry.name})
                          : tr(setting == 1 ? "Fast forward saved for the next game launch." : "Crash rescue saved for the next game launch.");
    return;
  }
  auto i = displayValues.as_object().find("performanceMetrics");
  int mode = 0;
  if (i != displayValues.as_object().end() && i->second.is_integer() &&
      i->second.as_integer() >= 0 && i->second.as_integer() <= 3)
    mode = i->second.as_integer();
  auto value = Json::integer_type(reset ? 0 : (mode + step + 4) % 4);
  if (gameOptions)
    model.changeGameSetting(gameEntry, "performanceMetrics", value, reset);
  else
    model.changeDisplay("performanceMetrics", value);
  displayValues = gameOptions ? model.gameSettings(gameEntry) : model.display();
  message = gameOptions
                ? trf(inGame ? "Saved for {}. Applied now." : "Saved for {}. Applies on next launch.", {gameEntry.name})
                : tr("Performance metrics saved for the next game launch.");
}
void UI::changeAudio(bool reset) {
  const char *keys[] = {"enableAudio", "midiReverb", "midiChorus"};
  auto found = displayValues.as_object().find(keys[setting]);
  bool value = setting == 0;
  if (found != displayValues.as_object().end() && found->second.is_boolean())
    value = found->second.as_boolean();
  model.changeGameSetting(gameEntry, keys[setting], Json(!value), reset);
  displayValues = model.gameSettings(gameEntry);
  message = trf(inGame ? "Saved for {}. Applied now." : "Saved for {}. Applies on next launch.", {gameEntry.name});
}
void UI::preparation(const std::string &label) {
  preparationLabel = tr(label);
  message = trf("{} - <circle> Circle: cancel", {preparationLabel});
  if (progressPump) progressPump();
}
void UI::progress(uint64_t done, uint64_t total) {
  auto label = preparationLabel.empty() ? std::string(tr("Preparing ZIP")) : preparationLabel;
  message = total ? trf("{}: {}% - <circle> Circle: cancel", {label, std::to_string(done * 100 / total)})
                  : trf("{}: reading contents - <circle> Circle: cancel", {label});
  if (progressPump) progressPump();
}
void UI::scanProgress(size_t count, const std::string &) {
  message = trf("Scanning folder {} - <circle> Circle: cancel", {std::to_string(count)});
  if (progressPump)
    progressPump();
}
void UI::chooseRtp(const std::string &path) {
  model.setRtp(packs[rtpSelected], path);
  browser.reset();
  focus = Focus::Settings;
  page = Page::Rtp;
  refreshRtp();
  message = trf("{} RTP location saved. Files stay in place.", {packLabel(packs[rtpSelected])});
}
void UI::browserAction(Button b) {
  if (b == Button::Up || b == Button::Down) {
    move(browserSelected, browserFirst, browser->list.size(), homeLayout::browserRows,
         b == Button::Up ? -1 : 1);
    return;
  }
  if (b == Button::Cross && !browser->list.empty()) {
    auto path = browser->selectedPath(browserSelected);
    if (browseMode == BrowseMode::Rtp &&
        !browser->list[browserSelected].directory)
      chooseRtp(path);
    else {
      browser->enter(path);
      browserSelected = browserFirst = 0;
      message.clear();
    }
    return;
  }
  if (b == Button::Circle || b == Button::Left) {
    if (!browser->back())
      browser.reset();
    browserSelected = browserFirst = 0;
    message.clear();
    return;
  }
  if (b == Button::Start) {
    browser.reset();
    message.clear();
    return;
  }
  if (b != Button::Square)
    return;
  if (browser->path.empty())
    throw std::runtime_error(tr(browseMode == BrowseMode::Rtp
                                    ? "Choose an RTP folder or ZIP first"
                                    : "Choose a game folder or collection first"));
  if (browseMode == BrowseMode::Rtp) {
    chooseRtp(browser->path);
    return;
  }
  auto callback = [this](size_t count, const std::string &path) {
    scanProgress(count, path);
  };
  auto added = browseMode == BrowseMode::Folder
                   ? model.addSearchFolder(browser->path, callback)
                   : model.addFolder(browser->path, callback);
  if (browseMode == BrowseMode::Folder) {
    focus = Focus::Settings;
    page = Page::Folders;
    setting = settingFirst = 0;
    refreshFolders();
  } else
    focus = Focus::Home;
  auto mode = browseMode;
  browser.reset();
  home.target = 0;
  home.position = 0;
  message = mode == BrowseMode::Folder
                ? trf("Extra game folder saved. Found {} games; files stay in place.", {std::to_string(added)})
                : added == 1 ? std::string(tr("Added 1 game. Files stay in their original folders."))
                             : trf("Added {} games. Files stay in their original folders.", {std::to_string(added)});
}
// Sound rows: menu sounds, their volume, title music, its volume. Volumes
// move in steps of 10 (Cross wraps around); Square restores the default.
void UI::changeSound(int step, bool reset) {
  auto level = [&](int &v, int fallback) {
    if (reset)
      v = fallback;
    else if (step > 0)
      v = v >= 100 ? 10 : std::min(100, v + 10);
    else
      v = std::max(10, v - 10);
  };
  if (setting == 0)
    menuSounds = reset ? true : !menuSounds;
  else if (setting == 1)
    level(effectsVolume, 70);
  else if (setting == 2)
    themeMusic = reset ? true : !themeMusic;
  else
    level(musicVolume, 80);
  savePreferences();
}
void UI::openSetting() {
  if(page==Page::Updates) {
    if(setting==0) {
      automaticUpdates=!automaticUpdates;
      Json saved(std::initializer_list<Json::pair_type>{});saved.as_object()["automatic"]=automaticUpdates;
      atomicWrite(model.paths.at("update-settings.json"),saved.stringify());
    } else if(setting==1) {checkReleaseRequested=true;message=tr("Checking releases...");}
    else if(!releaseNotice.tag.empty())showRelease();
    else message=tr("No newer release has been found.");
    return;
  }
  if (page == Page::Home) {
    if (setting == 6) {
      openPage(Page::Keys);
      return;
    }
    if (setting == 7) {
      changeStartView(1);
      return;
    }
    if (setting == 8) {
      changeLanguage(1);
      return;
    }
    if (setting == 9) {
      openWelcome();
      return;
    }
    if (setting == 10) {
      openPage(Page::Sound);
      return;
    }
    if (setting == 11) {
      exitRequested = true;
      return;
    }
    if (setting == 4) {openPage(Page::Metrics);return;}
    if (setting == 5) {openPage(Page::Updates);return;}
    if (setting == 3) {
      openPage(Page::Errors);
      return;
    }
    openPage(setting == 0   ? Page::Display
             : setting == 1 ? Page::Rtp
                            : Page::Folders);
    return;
  }
  if (page == Page::Sound) {
    changeSound(1);
    return;
  }
  if (page == Page::Keys) {
    if (setting == 0) {
      autoGameInfo = !autoGameInfo;
      savePreferences();
      message = tr(autoGameInfo ? "New games get their game info automatically." : "Game info downloads only when you ask.");
      if (!autoGameInfo && artQuiet)
        artFetch.reset();
    } else if (setting == 1) {
      blurAdult = !blurAdult;
      savePreferences();
      message = tr(blurAdult ? "Adult artwork is blurred." : "Adult artwork is shown as is.");
    } else
      editKey(setting - 2);
    return;
  }
  if (page == Page::Errors) {
    if (!errors.empty()) {
      model.activeErrorPath = errors[setting].path;
      errorReport = model.readError(model.activeErrorPath);
      errorScroll = 0;
      if (errorReport.empty())
        message = tr("This report could not be read.");
    }
    return;
  }
  if (page == Page::Display) {
    changeDisplay();
    return;
  }
  if (page == Page::Metrics) { changeMetrics(); return; }
  openBrowser(page == Page::Rtp ? BrowseMode::Rtp : BrowseMode::Folder);
  if (page == Page::Folders && setting > 0) {
    auto folders = gameFolders;
    if (size_t(setting - 1) < folders.size() &&
        directory(folders[setting - 1]))
      browser->enter(folders[setting - 1]);
  }
}
bool UI::offerRescue(const Entry &chosen, const Entry &source, bool fromHome) {
  auto found = model.rescue(chosen);
  if (!found.present)
    return false;
  rescueInfo = found;
  rescueFromHome = fromHome;
  if (fromHome) {
    gameMenuSource = source;
    gameEntry = chosen;
    displayValues = model.gameSettings(chosen);
    canFreeZip = false;
    gameOptions = true;
    focus = Focus::Settings;
  } else
    gameSetting = setting;
  page = Page::Rescue;
  setting = settingFirst = 0;
  message.clear();
  return true;
}
void UI::answerRescue() {
  auto chosen = gameEntry;
  if (setting == 2) {
    model.discardRescue(chosen);
    if (rescueFromHome) {
      gameOptions = false;
      page = Page::Home;
      focus = Focus::Home;
    } else {
      page = Page::Game;
      setting = gameSetting;
      settingFirst = std::max(0, setting - 6);
    }
    message = trf("Quick save of {} discarded.", {chosen.name});
    return;
  }
  model.requestResume(chosen, setting == 0);
  if (rescueFromHome)
    model.recordLaunch(entryKey(gameMenuSource));
  startGame(chosen);
}
void UI::startGame(const Entry &chosen) {
  launchingName = chosen.name;
  if (beginLaunch)
    beginLaunch();
  model.launch(chosen, [this](uint64_t done, uint64_t total) {
    progress(done, total);
  }, [this](const std::string &label) { preparation(label); });
  launchFormat = chosen.version;
  launchRequested = true;
}
void UI::play(size_t index) {
  if (index >= model.list.size())
    return;
  Entry chosen;
  auto e = model.list[index];
  if (comingSoon(e.version))
    throw std::runtime_error(comingSoonMessage(e.version));
  // A game with a crash rescue asks before anything starts.
  if (e.kind == Kind::Game && offerRescue(e, e, true))
    return;
  launchingName = e.name;
  if (beginLaunch && e.kind != Kind::Folder)
    beginLaunch();
  if (e.kind != Kind::Folder) preparation(tr(e.kind == Kind::Zip ? "Reading game archive" : "Reading game folder"));
  if (model.enter(e, chosen)) {
    model.launch(chosen, [this](uint64_t done, uint64_t total) {
      progress(done, total);
    }, [this](const std::string &label) { preparation(label); });
    model.recordLaunch(entryKey(e));
    launchFormat = chosen.version;
    launchRequested = true;
  } else {
    // A collection opened: its games start from the first.
    query.clear();
    message.clear();
    launchingName.clear();
    home.target = 0;
    home.position = 0;
  }
}
void UI::action(Button b) {
  InputFeedback feedback(*this, b);
  try {
    if (welcome.active) {
      welcomeAction(b);
      return;
    }
    if (homeActive()) {
      homeAction(b);
      return;
    }
    if(!releaseReport.empty()) {
      if(b==Button::Circle || b==Button::Cross)releaseReport.clear();
      if(b==Button::Up || b==Button::Down)releaseScroll=std::max(0,std::min(releaseScroll+(b==Button::Up?-1:1),std::max(0,int(releaseReport.size())-11)));
      return;
    }
    if (focus == Focus::About) {
      if (b == Button::Circle || b == Button::Left) {
        focus = Focus::Home;
        return;
      }
      if (b == Button::Up || b == Button::Down)
        scroll(b == Button::Up ? -1 : 1);
      return;
    }
    if (!errorReport.empty()) {
      if (b == Button::Cross || b == Button::Circle) {
        errorReport.clear();
        message.clear();
        return;
      }
      if (b == Button::Square) {
        message = trf("Saved: {}", {model.exportError()});
        return;
      }
      if (b == Button::Up || b == Button::Down)
        errorScroll =
            std::max(0, std::min(errorScroll + (b == Button::Up ? -1 : 1),
                                 std::max(0, int(errorReport.size()) - 12)));
      return;
    }
    if (!missingRtp.empty()) {
      if (b == Button::Cross) {
        rtpSelected = 0;
        for (int i = 0; i < rtpPackCount; ++i)
          if (missingRtp == packs[i])
            rtpSelected = i;
        openPage(Page::Rtp);
        focus = Focus::Settings;
        missingRtp.clear();
      } else if (b == Button::Circle)
        missingRtp.clear();
      return;
    }
    if (browser) {
      browserAction(b);
      return;
    }
    if (gameOptions) { gameAction(b); return; }
    // What is left is the settings sheet and its pages.
    if (focus != Focus::Settings)
      return;
    if (b == Button::Start) {
      openBrowser(BrowseMode::Games);
      return;
    }
    if (b == Button::Up || b == Button::Down) {
      int step = b == Button::Up ? -1 : 1;
      if (page == Page::Rtp)
        rtpSelected = (rtpSelected + step + rtpPackCount) % rtpPackCount;
      else
        move(setting, settingFirst,
             page == Page::Errors    ? errors.size()
             : page == Page::Folders ? 1 + gameFolders.size()
             : page == Page::Home    ? settingsRows()
               : page == Page::Keys    ? keysRows
             : page == Page::Sound   ? soundRows
             : page == Page::Metrics ? 3
                                     : 3,
             7, step);
      message.clear();
      return;
    }
    if (b == Button::Cross) {
      openSetting();
      return;
    }
    if (b == Button::Square) {
      if (page == Page::Errors) {
        if (!errors.empty()) {
          model.activeErrorPath = errors[setting].path;
          message = trf("Saved: {}", {model.exportError()});
        }
      } else if (page == Page::Keys && setting > 1) {
        std::remove(keyFile(setting - 2).c_str());
        message = tr("Key removed.");
      } else if (page == Page::Rtp) {
        model.setRtp(packs[rtpSelected]);
        refreshRtp();
        message = tr("Automatic RTP location restored. Pack files were not removed.");
      } else if (page == Page::Sound)
        changeSound(1, true);
      else if (page == Page::Display)
        changeDisplay(1, true);
      else if (page == Page::Metrics)
        changeMetrics(1, true);
      else if (page == Page::Folders && setting > 0) {
        auto paths = gameFolders;
        if (size_t(setting - 1) < paths.size())
          model.removeSearchFolder(paths[setting - 1]);
        refreshFolders();
        setting = settingFirst = 0;
        message = tr("Game folder reference removed. Games and saves stay in place.");
      }
      return;
    }
    if ((b == Button::Left || b == Button::Right) && page == Page::Home && setting == 7) {
      changeStartView(b == Button::Left ? -1 : 1);
      return;
    }
    if ((b == Button::Left || b == Button::Right) && page == Page::Home && setting == 8) {
      changeLanguage(b == Button::Left ? -1 : 1);
      return;
    }
    if ((b == Button::Left || b == Button::Right) && page == Page::Sound) {
      changeSound(b == Button::Left ? -1 : 1);
      return;
    }
    if ((b == Button::Left || b == Button::Right) &&
        (page == Page::Display || page == Page::Metrics)) {
      if (page == Page::Metrics) changeMetrics(b == Button::Left ? -1 : 1);
      else changeDisplay(b == Button::Left ? -1 : 1);
      return;
    }
    if (b == Button::Triangle) {
      if (page == Page::Rtp) {
        model.clearRtpCache();
        refreshRtp();
        message = tr("Checking RTP locations...");
      }
      return;
    }
    if (b == Button::Circle || b == Button::Left) {
      if (page == Page::Home) {
        focus = Focus::Home;
        message.clear();
      } else
        openPage(Page::Home);
    }
  } catch (const MissingRtp &e) {
    launchingName.clear();
    missingRtp = e.pack;
  } catch (const std::exception &e) {
    launchingName.clear();
    message = e.what();
  }
}
} // namespace hardrpg
