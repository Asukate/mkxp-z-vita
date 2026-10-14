#pragma once
#include "model.h"
#include "update_check.h"
#include <memory>

namespace hardrpg {
enum class Button {
  None,
  Up,
  Down,
  Cross,
  Circle,
  Square,
  Triangle,
  Left,
  Right,
  Start,
  Select,
  L,
  R,
  Paths
};
enum class Focus { Menu, Games, Settings, About, Sources };
enum class Page {
  Home,
  Display,
  Rtp,
  Folders,
  Errors,
  Metrics,
  Controls,
  Updates,
  Game,
  Audio,
  GameInfo,
  Remove
};
enum class BrowseMode { Games, Rtp, Folder };
// This state machine is shared by the Vita front end and the desktop preview.
// It owns no Ruby values, RGSS objects, audio devices, or engine threads.
struct RtpCheck;
class UI {
public:
  explicit UI(Paths paths = {});
  Model model;
  int menu = 0, selected = 0, first = 0, setting = 0, settingFirst = 0,
      rtpSelected = 0;
  int engineFilter = 0, aboutScroll = 0, sourceScroll = 0;
  std::string query, launchingName, preparationLabel;
  std::vector<ErrorRecord> errors;
  std::vector<std::string> sources;
  int rtpPending = -1;
  std::shared_ptr<RtpCheck> rtpCheck;
  unsigned rtpGeneration = 0;
  int browserSelected = 0, browserFirst = 0, errorScroll = 0;
  Focus focus = Focus::Menu;
  Page page = Page::Home;
  BrowseMode browseMode = BrowseMode::Games;
  std::unique_ptr<Browser> browser;
  std::string message, missingRtp;
  std::vector<std::string> errorReport;
  Json displayValues, rtpChoices;
  Entry controlsEntry;
  Entry gameEntry, gameMenuSource;
  bool gameOptions = false;
  int gameSetting = 0;
  // The last row, deleting the ZIP of an unpacked ZIP game, shows only then.
  bool canFreeZip = false;
  int gameOptionsCount() const { return canFreeZip ? 10 : 9; }
  void openGameMenu();
  // "Remove from library" takes the game off the list, then asks about its
  // files one question at a time: the game files (folder or ZIP), then the
  // unpacked copy of a ZIP game. Each question defaults to No.
  enum RemoveQuestion { DeleteFiles, DeleteUnpacked, DeleteZipOnly };
  Model::Removal removeInfo;
  std::vector<RemoveQuestion> removeQuestions;
  size_t removeStep = 0;
  bool removeFiles = false, removeUnpacked = false;
  void openRemove();
  void openFreeZip();
  void answerRemove(bool yes);
  void finishRemove();
  std::string removePath(RemoveQuestion) const;
  // Rows on the current page of the game settings.
  int optionRows() const;
  ControlProfile controls;
  ControlState controlsTestState;
  ControlSample controlsSample;
  ControlResult controlsResult;
  bool controlsTesting = false;
  // Choosing a keyboard key for the selected input on the on-screen keyboard.
  bool controlPicking = false;
  int controlPickKey = 0;
  // Showing the touch, stick and pointer list instead of the buttons.
  bool controlsAdvanced = false;
  // Showing a touch surface's page (1 front screen, 2 rear pad): its mode
  // and a diagram of the zones that mode reads, each mappable.
  int controlsPad = 0;
  // The zones the shown pad's mode reads, as a touchZones mask, and which
  // zone a row of the page is (row 0 is the mode).
  unsigned padMask() const {
    return touchZones(controlsPad == 2, controlsPad == 2 ? controls.rear : controls.front);
  }
  int padZone(int row) const {
    unsigned mask = padMask();
    for (int zone = 0; zone < 4; ++zone)
      if (mask & (1u << zone) && --row == 0)
        return zone;
    return -1;
  }
  // Where each row of the pad page is drawn and touched: the mode bar, then
  // the zones laid out as the surface is, left and right as seen when
  // holding the console.
  void padRowRect(int row, int &x, int &y, int &w, int &h) const {
    if (row == 0) {
      x = 184; y = 151; w = 744; h = 35;
      return;
    }
    int zone = padZone(row);
    unsigned mask = padMask();
    x = 228; y = 222; w = 640; h = 204;
    if (zone < 0)
      return;
    if (mask == 1 || mask == 2)
      return;
    w = 314;
    x += zone % 2 ? 326 : 0;
    if (mask == 15) {
      h = 96;
      y += zone >= 2 ? 108 : 0;
    }
  }
  // The mapping the selected controls row edits, or null for other rows.
  int *controlTarget(int row);
  void pickControlKey(Button);
  void openControlPicker();
  void assignControl(int value);
  int controlsCount() const;
  bool automaticUpdates = true, checkReleaseRequested = false;
  ReleaseNotice releaseNotice;
  std::vector<std::string> releaseReport;
  int releaseScroll = 0;
  void offerRelease(const ReleaseNotice &);
  void showRelease();
  std::string controlLabel(int row) const;
  std::string controlValue(int row) const;
  void openControls();
  void changeControl(int step);
  std::vector<std::string> gameFolders;
  std::vector<std::string> rtpStatuses;
  std::vector<std::pair<int, int>> history;
  int launchFormat = 0;
  bool launchRequested = false, exitRequested = false;
  std::function<void()> progressPump;
  // Returning false cancels without changing the persistent display name.
  std::function<bool(const std::string &, std::string &)> editName;
  std::function<bool(const std::string &, std::string &)> searchText;
  std::function<void()> beginLaunch;
  void launchFailed(int code, const std::string &executable);
  void action(Button);
  std::vector<size_t> visible() const;
  int gameRows() const { return 9; }
  int gameTop() const { return 180; }
  bool service();
  void search(const std::string &);
  void touch(int x, int y);
  void scroll(int rows);
  void openPage(Page);
  void refreshRtp();
  void refreshFolders();

private:
  void resetSelection();
  void openBrowser(BrowseMode);
  void browserAction(Button);
  void select();
  void changeDisplay(int step = 1, bool reset = false);
  void changeMetrics(int step = 1, bool reset = false);
  void changeAudio(bool reset = false);
  void renameGame(const Entry &);
  void gameAction(Button);
  void progress(uint64_t, uint64_t);
  void preparation(const std::string &);
  void scanProgress(size_t, const std::string &);
  void chooseRtp(const std::string &);
};
} // namespace hardrpg
