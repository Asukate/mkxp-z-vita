#include "ui.h"
#include "release.h"
#include <algorithm>
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
UI::UI(Paths paths)
    : model(std::move(paths)),
      displayValues(std::initializer_list<Json::pair_type>{}),
      rtpChoices(std::initializer_list<Json::pair_type>{}) {
  sources = model.libraryRoots();
  try {auto text=readFile(model.paths.at("update-settings.json"),4096);
    if(!text.empty())automaticUpdates=json5pp::parse(text).as_object().at("automatic").as_boolean();
  }catch(...) {}
  errorReport = model.takeError();
  if (errorReport.empty())
    model.takeError(true);
}
void UI::launchFailed(int code, const std::string &executable) {
  launchRequested = false;
  launchFormat = 0;
  launchingName.clear();
  preparationLabel.clear();
  char hexadecimal[16];
  std::snprintf(hexadecimal, sizeof(hexadecimal), "0x%08x", unsigned(code));
  message = "Could not start game (LoadExec " + std::to_string(code) +
            ", " + hexadecimal + "; " + executable + ").";
}
void UI::resetSelection() {
  selected = first = 0;
  message.clear();
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
    catch (...) { result = "Unavailable"; error = "RTP check failed."; }
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
      throw std::runtime_error("Cannot start RTP location check.");
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
  if(errorReport.empty() && missingRtp.empty() && !browser && !gameOptions)showRelease();
}
void UI::showRelease() {
  if(releaseNotice.tag.empty())return;
  releaseReport.clear();releaseScroll=0;
  releaseReport.push_back("Installed: " HARDRPG_VERSION_TAG);
  releaseReport.push_back("Available: "+releaseNotice.tag);
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
  releaseReport.push_back("");releaseReport.push_back("Download on your PC:");
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
  selected = first = 0;
}
void UI::scroll(int rows) {
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
  if (menu == 0 && focus != Focus::Sources) {
    focus = Focus::Games;
    auto count = int(visible().size());
    first =
        std::max(0, std::min(first + rows, std::max(0, count - gameRows())));
    selected = std::max(
        first, std::min(selected, std::min(count - 1, first + gameRows() - 1)));
    if (!count)
      selected = first = 0;
  } else if (focus == Focus::About) {
    aboutScroll = std::max(
        0, std::min(aboutScroll + rows, std::max(0, int(sizeof(releaseNotes) /
                                                        sizeof(*releaseNotes)) -
                                                        9)));
  } else if (focus == Focus::Sources) {
    int lines = 0;
    for (auto &s : sources)
      lines += (s.size() + 71) / 72 + 1;
    sourceScroll =
        std::max(0, std::min(sourceScroll + rows, std::max(0, lines - 12)));
  } else if (menu == 1 && focus == Focus::Settings) {
    if (page == Page::Rtp) {
      rtpSelected = std::max(0, std::min(rtpSelected + rows, rtpPackCount - 1));
      return;
    }
    int count = page == Page::Errors    ? errors.size()
                : page == Page::Folders ? 1 + gameFolders.size()
                : page == Page::Home    ? 6
                : page == Page::Metrics ? 2
                                        : 3;
    setting = std::max(0, std::min(setting + rows, std::max(0, count - 1)));
    settingFirst = std::max(0, std::min(settingFirst, setting));
    if (setting >= settingFirst + 7)
      settingFirst = setting - 6;
  }
}
void UI::touch(int x, int y) {
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
  if(page==Page::Controls) {if(!controlsTesting && x>=184 && x<918 && y>=151 && y<445) {int row=settingFirst+(y-151)/42;if(row<controlsCount()) {bool activate=setting==row;setting=row;if(activate) {if(controlTarget(row))openControlPicker();else changeControl(1);}}}return;}
  if (controlsTesting || browser || !errorReport.empty() || !releaseReport.empty() || !missingRtp.empty())
    return;
  if (gameOptions) {
    if (x >= 184 && x < 918 && y >= 151 && y < 445 && page != Page::GameInfo) {
      int count = optionRows();
      int row = settingFirst + (y - 151) / (page == Page::Rtp ? 54 : 42);
      if (row < count) {
        int &target = page == Page::Rtp ? rtpSelected : setting;
        bool activate = target == row;
        target = row;
        if (activate) action(Button::Cross);
      }
    }
    return;
  }
  if (x >= 22 && x < 152 && y >= 92 && y < 244) {
    menu = (y - 92) / 38;
    focus = Focus::Menu;
    page = Page::Home;
    return;
  }
  if (x < 184 || x > 932)
    return;
  if (focus == Focus::Sources) {
    action(Button::Circle);
    return;
  }
  if (menu == 0) {
    if (y < 146) {
      action(Button::Paths);
      return;
    }
    if (y < 178) {
      action(x < 580 ? Button::Select : Button::R);
      return;
    }
    int row = (y - gameTop()) / 32;
    auto list = visible();
    if (y >= gameTop() && row < gameRows() && first + row < int(list.size())) {
      if (x >= 918) {
        int count = list.size();
        first = std::max(
            0, std::min((y - gameTop()) * std::max(0, count - gameRows()) /
                            (gameRows() * 32),
                        std::max(0, count - gameRows())));
        selected = first;
      } else {
        bool activate = focus == Focus::Games && selected == first + row;
        focus = Focus::Games;
        selected = first + row;
        if (activate)
          action(Button::Cross);
      }
    }
  } else if (menu == 1) {
    int top = page == Page::Folders ? 183 : 151;
    int row = (y - top) / (page == Page::Rtp       ? 54
                           : page == Page::Folders ? 36
                                                   : 42);
    int count = page == Page::Home      ? 6
                : page == Page::Updates ? 3
                : page == Page::Display ? 3
                : page == Page::Metrics ? 2
                : page == Page::Rtp ? rtpPackCount
                : page == Page::Folders ? 1 + gameFolders.size()
                                        : errors.size();
    if (y >= top && row < (page == Page::Rtp ? rtpPackCount : 7) &&
        row + settingFirst < count) {
      int target = row + settingFirst;
      bool activate = focus == Focus::Settings &&
                      (page == Page::Rtp ? rtpSelected : setting) == target;
      focus = Focus::Settings;
      if (page == Page::Rtp)
        rtpSelected = target;
      else
        setting = target;
      if (activate)
        action(Button::Cross);
    }
  } else if (menu == 2)
    focus = Focus::About;
}
void UI::openControls() {
  auto entry = gameEntry;
  auto profile = model.controls(entry);
  controlsEntry = entry;
  controls = profile;
  menu = 0;
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
void UI::openGameMenu() {
  auto shown = visible();
  if (shown.empty() || selected < 0 || size_t(selected) >= shown.size())
    return;
  auto source = model.list[shown[selected]];
  Entry entry;
  if (!configurableGame(source, entry))
    throw std::runtime_error(
        "Open this collection and highlight a game first.");
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
  menu = 0;
  page = Page::Game;
  focus = Focus::Settings;
  message.clear();
}
int UI::optionRows() const {
  return page == Page::Game       ? gameOptionsCount()
         : page == Page::Metrics  ? 2
         : page == Page::Rtp      ? rtpPackCount
         : page == Page::GameInfo ? 0
         : page == Page::Remove   ? 2
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
    focus = Focus::Games;
    menu = 0;
    message = "ZIP deleted. " + name + " plays from its unpacked copy.";
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
  focus = Focus::Games;
  menu = 0;
  auto shown = visible();
  selected = std::max(0, std::min(selected, int(shown.size()) - 1));
  first = std::max(0, std::min(first, selected));
  message = removeFiles || removeUnpacked
                ? name + " removed and its files deleted."
                : name + " removed from the list. Files kept; Start adds it again.";
}
void UI::renameGame(const Entry &entry) {
  if (!editName)
    throw std::runtime_error("Game name keyboard is unavailable.");
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
  auto shown = visible();
  selected = first = 0;
  for (size_t i = 0; i < shown.size(); ++i)
    if (model.list[shown[i]].node.identity(model.paths) == id) {
      selected = i;
      if (selected >= first + gameRows())
        first = selected - gameRows() + 1;
      break;
    }
  message =
      name.empty() ? "Original game name restored." : "Display name saved.";
}
void UI::gameAction(Button b) {
  if (page == Page::Rtp && b == Button::Triangle) {
    model.clearRtpCache(); refreshRtp(); message = "Checking RTP locations..."; return;
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
  if (b == Button::Circle) {
    if (page == Page::Game) {
      gameOptions = false;
      page = Page::Home;
      focus = Focus::Games;
    } else {
      page = Page::Game;
      setting = gameSetting;
      settingFirst = std::max(0, setting - 6);
    }
    menu = 0;
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
      launchingName = gameEntry.name;
      if (beginLaunch)
        beginLaunch();
      model.launch(gameEntry, [this](uint64_t done, uint64_t total) {
        progress(done, total);
      }, [this](const std::string &label) { preparation(label); });
      launchFormat = gameEntry.version;
      launchRequested = true;
      break;
    }
    return;
  }
  if (page == Page::Remove && b == Button::Cross) {
    answerRemove(setting == 1);
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
        message = "Automatic RTP location restored.";
      } else if (b == Button::Cross)
        openBrowser(BrowseMode::Rtp);
    }
  }
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
    message = controlLabel(setting) + " now does " + controlName(value) +
              ". Custom controls are on; applies on next launch.";
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
    "Off", "Two drawn corner buttons", "Mouse pointer", "D-pad + drawn button",
    "Whole screen", "Left / right halves", "Four corners"};
static const char *const rearModeNames[] = {
    "Off", "Two upper zones", "Trackpad (hold L)", "Four corners",
    "Whole pad", "Left / right halves"};
std::string UI::controlLabel(int row) const {
  if (controlsPad) {
    if (row == 0)
      return controlsPad == 2 ? "Rear pad" : "Front screen";
    unsigned mask = padMask();
    int zone = padZone(row);
    const char *names[] = {"top left", "top right", "bottom left", "bottom right"};
    std::string where = mask == 15 ? names[zone]
                        : mask == 3 ? (zone ? "right" : "left")
                        : controlsPad == 1 && controls.front == 3 ? "button"
                        : controlsPad == 2 ? "whole pad" : "whole screen";
    return std::string(controlsPad == 2 ? "Rear " : "Front ") + where;
  }
  if (!controlsAdvanced) {
    if (row >= 1 && row <= 12)
      return std::string(controlSources[row - 1]) + " button";
    return row == 0 ? "Custom controls" : row == 13 ? "Tap to walk"
           : row == 14 ? "Front touch screen"
           : row == 15 ? "Rear touch pad"
           : row == 16 ? "Sticks and pointer"
           : row == 17 ? "Test inputs" : "Reset everything";
  }
  const char *labels[] = {"Stick deadzone", "Right stick moves pointer",
                          "Pointer speed", "Square toggles its action"};
  return row >= 0 && row < 4 ? labels[row] : "";
}
std::string UI::controlValue(int row) const {
  if (controlsPad) {
    if (row == 0)
      return controlsPad == 2 ? rearModeNames[controls.rear] : frontModeNames[controls.front];
    int zone = padZone(row);
    return zone >= 0 ? controlName(controls.zone(controlsPad == 2, zone)) : "";
  }
  if (!controlsAdvanced) {
    if (row >= 1 && row <= 12)
      return controlName(controls.buttons[row - 1]);
    return row == 0 ? (controls.enabled ? "On" : "Off (game's own controls)")
           : row == 13 ? (controls.tapToWalk ? "On (tap the map to walk there)" : "Off")
           : row == 14 ? frontModeNames[controls.front]
           : row == 15 ? rearModeNames[controls.rear]
           : row == 16 || row == 17 ? "Open" : "Back to defaults, off";
  }
  switch (row) {
  case 0:
    return std::to_string(controls.deadzone);
  case 1:
    return controls.rightPointer ? "On" : "Off";
  case 2:
    return std::to_string(controls.speed);
  default:
    return controls.toggleRun ? "On" : "Off";
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
      message = "Tap to walk turned off: the screen is touch zones now.";
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
        message = "Tap to walk on; front touch zones turned off. Next launch.";
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
  model.saveControls(controlsEntry,controls);
  if(message.empty())
    message=setting==13 && !controlsAdvanced && !controlsPad && controls.tapToWalk
            ?"Tap to walk on: works in games with standard tile movement. Next launch."
            :"Saved for "+controlsEntry.name+". Applies on next launch.";
}
void UI::refreshFolders() { gameFolders = model.searchFolders(); }
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
  Json v = setting == 2 ? Json(Json::integer_type(0)) : Json(defaultBool);
  if (i != displayValues.as_object().end())
    v = i->second;
  if (reset)
    v = setting == 2 ? Json(Json::integer_type(0)) : Json(defaultBool);
  else if (setting == 2) {
    int n = v.is_integer() && v.as_integer() >= 0 && v.as_integer() <= 1
                ? v.as_integer()
                : 0;
    v = Json::integer_type((n + step + 2) % 2);
  } else
    v = v.is_boolean() ? !v.as_boolean() : !defaultBool;
  if (gameOptions) model.changeGameSetting(gameEntry, k, v, reset);
  else model.changeDisplay(k, v);
  displayValues = gameOptions ? model.gameSettings(gameEntry) : model.display();
  message = gameOptions
                ? "Saved for " + gameEntry.name + ". Applies on next launch."
                : "Display settings saved for the next game launch.";
}
void UI::changeMetrics(int step, bool reset) {
  if (setting == 1) {
    auto found = displayValues.as_object().find("fastForward");
    bool on = found == displayValues.as_object().end() || !found->second.is_boolean() ||
              found->second.as_boolean();
    Json value(reset ? true : !on);
    if (gameOptions)
      model.changeGameSetting(gameEntry, "fastForward", value, reset);
    else
      model.changeDisplay("fastForward", value);
    displayValues = gameOptions ? model.gameSettings(gameEntry) : model.display();
    message = gameOptions
                  ? "Saved for " + gameEntry.name + ". Applies on next launch."
                  : "Fast forward saved for the next game launch.";
    return;
  }
  auto i = displayValues.as_object().find("performanceMetrics");
  int mode = 0;
  if (i != displayValues.as_object().end() && i->second.is_integer() &&
      i->second.as_integer() >= 0 && i->second.as_integer() <= 2)
    mode = i->second.as_integer();
  auto value = Json::integer_type(reset ? 0 : (mode + step + 3) % 3);
  if (gameOptions)
    model.changeGameSetting(gameEntry, "performanceMetrics", value, reset);
  else
    model.changeDisplay("performanceMetrics", value);
  displayValues = gameOptions ? model.gameSettings(gameEntry) : model.display();
  message = gameOptions
                ? "Saved for " + gameEntry.name + ". Applies on next launch."
                : "Performance metrics saved for the next game launch.";
}
void UI::changeAudio(bool reset) {
  const char *keys[] = {"enableAudio", "midiReverb", "midiChorus"};
  auto found = displayValues.as_object().find(keys[setting]);
  bool value = setting == 0;
  if (found != displayValues.as_object().end() && found->second.is_boolean())
    value = found->second.as_boolean();
  model.changeGameSetting(gameEntry, keys[setting], Json(!value), reset);
  displayValues = model.gameSettings(gameEntry);
  message = "Saved for " + gameEntry.name + ". Applies on next launch.";
}
void UI::preparation(const std::string &label) {
  preparationLabel = label;
  message = label + " - Circle: cancel";
  if (progressPump) progressPump();
}
void UI::progress(uint64_t done, uint64_t total) {
  auto label = preparationLabel.empty() ? "Preparing ZIP" : preparationLabel;
  message = label + (total ? ": " + std::to_string(done * 100 / total) + "%"
                          : ": reading contents") + " - Circle: cancel";
  if (progressPump) progressPump();
}
void UI::scanProgress(size_t count, const std::string &) {
  message = "Scanning folder " + std::to_string(count) + " - Circle: cancel";
  if (progressPump)
    progressPump();
}
void UI::chooseRtp(const std::string &path) {
  model.setRtp(packs[rtpSelected], path);
  browser.reset();
  menu = gameOptions ? 0 : 1;
  focus = Focus::Settings;
  page = Page::Rtp;
  refreshRtp();
  message = packLabel(packs[rtpSelected]) +
            " RTP location saved. Files stay in place.";
}
void UI::browserAction(Button b) {
  if (b == Button::Up || b == Button::Down) {
    move(browserSelected, browserFirst, browser->list.size(), 10,
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
    throw std::runtime_error(browseMode == BrowseMode::Rtp
                                 ? "Choose an RTP folder or ZIP first"
                                 : "Choose a game folder or collection first");
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
    menu = 1;
    focus = Focus::Settings;
    page = Page::Folders;
    setting = settingFirst = 0;
    refreshFolders();
  } else {
    menu = 0;
    focus = Focus::Games;
  }
  sources = model.libraryRoots();
  auto mode = browseMode;
  browser.reset();
  history.clear();
  resetSelection();
  message = mode == BrowseMode::Folder
                ? "Extra game folder saved. Found " + std::to_string(added) +
                      " games; files stay in place."
                : "Added " + std::to_string(added) + " game" +
                      (added == 1 ? "" : "s") +
                      ". Files stay in their original folders.";
}
void UI::select() {
  if (focus == Focus::Menu) {
    if (menu == 3) {
      exitRequested = true;
      return;
    }
    if (menu == 0)
      focus = Focus::Games;
    if (menu == 2)
      focus = Focus::About;
    if (menu == 1) {
      focus = Focus::Settings;
      openPage(Page::Home);
    }
    return;
  }
  if (focus == Focus::Settings) {
    if(page==Page::Updates) {
      if(setting==0) {
        automaticUpdates=!automaticUpdates;
        Json saved(std::initializer_list<Json::pair_type>{});saved.as_object()["automatic"]=automaticUpdates;
        atomicWrite(model.paths.at("update-settings.json"),saved.stringify());
      } else if(setting==1) {checkReleaseRequested=true;message="Checking releases...";}
      else if(!releaseNotice.tag.empty())showRelease();
      else message="No newer release has been found.";
      return;
    }
    if (page == Page::Home) {
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
    if (page == Page::Errors) {
      if (!errors.empty()) {
        model.activeErrorPath = errors[setting].path;
        errorReport = model.readError(model.activeErrorPath);
        errorScroll = 0;
        if (errorReport.empty())
          message = "This report could not be read.";
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
    return;
  }
  auto list = visible();
  if (list.empty())
    return;
  Entry chosen;
  auto e = model.list[list[selected]];
  launchingName = e.name;
  if (beginLaunch && e.kind != Kind::Folder)
    beginLaunch();
  if (e.kind != Kind::Folder) preparation(e.kind == Kind::Zip ? "Reading game archive" : "Reading game folder");
  if (model.enter(e, chosen)) {
    model.launch(chosen, [this](uint64_t done, uint64_t total) {
      progress(done, total);
    }, [this](const std::string &label) { preparation(label); });
    launchFormat = chosen.version;
    launchRequested = true;
  } else {
    history.push_back({selected, first});
    query.clear();
    resetSelection();
    launchingName.clear();
  }
}
void UI::action(Button b) {
  try {
    if(!releaseReport.empty()) {
      if(b==Button::Circle || b==Button::Cross)releaseReport.clear();
      if(b==Button::Up || b==Button::Down)releaseScroll=std::max(0,std::min(releaseScroll+(b==Button::Up?-1:1),std::max(0,int(releaseReport.size())-11)));
      return;
    }
    if (focus == Focus::Sources) {
      if (b == Button::Circle || b == Button::Paths) {
        focus = Focus::Menu;
        return;
      }
      if (b == Button::Up)
        sourceScroll = std::max(0, sourceScroll - 1);
      if (b == Button::Down) {
        int lines = 0;
        for (auto &s : sources)
          lines += (s.size() + 71) / 72 + 1;
        sourceScroll = std::min(sourceScroll + 1, std::max(0, lines - 12));
      }
      return;
    }
    if (focus == Focus::About) {
      if (b == Button::Circle || b == Button::Left) {
        focus = Focus::Menu;
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
        message = "Saved: " + model.exportError();
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
        menu = gameOptions ? 0 : 1;
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
    if (b == Button::Start) {
      openBrowser(BrowseMode::Games);
      return;
    }
    if (menu == 0 && b == Button::Paths) {
      sources = model.libraryRoots();
      focus = Focus::Sources;
      sourceScroll = 0;
      return;
    }
    if (menu == 0 && b == Button::Select) {
      if (searchText) {
        std::string out;
        if (searchText(query, out)) {
          search(out);
          focus = Focus::Games;
        }
      }
      return;
    }
    if (menu == 0 && (b == Button::L || b == Button::R)) {
      engineFilter =
          (engineFilter + (b == Button::R ? 1 : engineFilterCount - 1)) %
          engineFilterCount;
      selected = first = 0;
      return;
    }
    if (b == Button::Up || b == Button::Down) {
      int step = b == Button::Up ? -1 : 1;
      if (focus == Focus::Menu)
        menu = (menu + step + 4) % 4;
      else if (focus == Focus::Settings) {
        if (page == Page::Rtp)
          rtpSelected = (rtpSelected + step + rtpPackCount) % rtpPackCount;
        else
          move(setting, settingFirst,
               page == Page::Errors    ? errors.size()
               : page == Page::Folders ? 1 + gameFolders.size()
               : page == Page::Home    ? 6
               : page == Page::Metrics ? 2
                                       : 3,
               7, step);
      } else
        move(selected, first, visible().size(), gameRows(), step);
      message.clear();
      return;
    }
    if (b == Button::Cross) {
      select();
      return;
    }
    if (b == Button::Right && focus == Focus::Menu && menu < 3) {
      select();
      return;
    }
    if (b == Button::Square && focus == Focus::Games && !visible().empty()) {
      auto e = model.list[visible()[selected]];
      if (e.kind == Kind::Folder)
        throw std::runtime_error("Select a game or ZIP to rename.");
      renameGame(e);
      return;
    }
    if (b == Button::Square && focus == Focus::Settings) {
      if (page == Page::Errors) {
        if (!errors.empty()) {
          model.activeErrorPath = errors[setting].path;
          message = "Saved: " + model.exportError();
        }
      } else if (page == Page::Rtp) {
        model.setRtp(packs[rtpSelected]);
        refreshRtp();
        message =
            "Automatic RTP location restored. Pack files were not removed.";
      } else if (page == Page::Display)
        changeDisplay(1, true);
      else if (page == Page::Metrics)
        changeMetrics(1, true);
      else if (page == Page::Folders && setting > 0) {
        auto paths = gameFolders;
        if (size_t(setting - 1) < paths.size())
          model.removeSearchFolder(paths[setting - 1]);
        refreshFolders();
        setting = settingFirst = 0;
        message =
            "Game folder reference removed. Games and saves stay in place.";
      }
      return;
    }
    if ((b == Button::Left || b == Button::Right) && focus == Focus::Settings &&
        (page == Page::Display || page == Page::Metrics)) {
      if (page == Page::Metrics) changeMetrics(b == Button::Left ? -1 : 1);
      else changeDisplay(b == Button::Left ? -1 : 1);
      return;
    }
    if (b == Button::Triangle) {
      if (focus == Focus::Settings && page == Page::Rtp) {
        model.clearRtpCache();
        refreshRtp();
        message = "Checking RTP locations...";
      } else if (focus == Focus::Games && menu == 0)
        openGameMenu();
      return;
    }
    if (b == Button::Circle || b == Button::Left) {
      if (focus == Focus::Settings) {
        if (page == Page::Home) {
          focus = Focus::Menu;
          message.clear();
        } else
          openPage(Page::Home);
      } else if (focus == Focus::Games) {
        if (model.back()) {
          auto pos = history.empty() ? std::make_pair(0, 0) : history.back();
          if (!history.empty())
            history.pop_back();
          selected = pos.first;
          first = pos.second;
          if (size_t(selected) >= visible().size())
            selected = first = 0;
          message.clear();
        } else {
          focus = Focus::Menu;
          message.clear();
        }
      } else {
        menu = 0;
        message.clear();
      }
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
