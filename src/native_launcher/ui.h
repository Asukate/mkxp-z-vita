#pragma once
#include "art_fetch.h"
#include "model.h"
#include "update_check.h"
#include <map>
#include <memory>
#include <set>

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
// Home is the home screen; Settings and About are sheets over it.
enum class Focus { Home, Settings, About };
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
  Remove,
  Keys // artwork source keys
};
enum class BrowseMode { Games, Rtp, Folder };
// The home screen: a centred cover carousel over the library, with an
// in-place details panel, an options menu and a filter panel.
struct Home {
  enum Sort { Recent, Name, PlayTime, SortCount };
  enum Panel { NoPanel, Filter, Options, Artwork };
  enum Zone { Shelf, Toolbar, Top };
  bool grid = false;
  int sort = Recent, direction = 1;
  bool favouritesOnly = false, unplayedOnly = false;
  unsigned hiddenEngines = 0; // bit n hides format n (1 XP ... 4 2000/2003)
  double position = 0;        // animated carousel position, in cards
  int target = 0;             // the card the carousel settles on
  bool details = false;
  double detailsAmount = 0;   // 0 shelf centred, 1 details open
  double sideAmount = 0;      // 1 while the filter panel holds the right half
  Panel panel = NoPanel;
  int panelFocus = 0;
  Zone zone = Shelf;
  int toolbarFocus = 0, topFocus = 0;
  // Front touch: a press becomes a drag after 10 px; flings keep momentum.
  bool pressed = false, dragging = false;
  int pressX = 0, pressY = 0, lastX = 0;
  double dragFrom = 0, velocity = 0;
  uint32_t lastMove = 0;
};
// This state machine is shared by the Vita front end and the desktop preview.
// It owns no Ruby values, RGSS objects, audio devices, or engine threads.
struct RtpCheck;
class UI {
public:
  // scan = false defers reading the library to scanLibrary(), which the
  // launcher runs on a worker thread while the screen starts up.
  explicit UI(Paths paths = {}, bool scan = true);
  void scanLibrary();
  Model model;
  // Home screen state (see home.cpp).
  Home home;
  std::map<std::string, Activity> activity;
  std::set<std::string> favourites;
  void loadHome();
  bool homeActive() const;
  // Library entries shown on the home screen, in order (indices into model.list).
  std::vector<size_t> homeList() const;
  // The entry shown in the middle of the carousel, or null on the Add card.
  const Entry *homeEntry() const;
  std::string entryKey(const Entry &) const;
  // Engine tabs present in the library: 0 is All, then format numbers.
  std::vector<int> engineTabs() const;
  void homeAction(Button);
  void homeTap(int x, int y);
  // state 0 press, 1 move, 2 release; ms is a monotonic clock.
  bool homePointer(int state, int x, int y, uint32_t ms);
  bool animating() const;
  void tick(double seconds);
  // Artwork downloads (see art_fetch.h).
  std::unique_ptr<ArtFetch> artFetch;
  mutable std::vector<std::string> artUpdated; // games whose art the view must reload
  // The folders under art/ that may hold a game's pictures, best first.
  std::vector<std::string> artFolders(const Entry &) const;
  void fetchArt(bool all);
  bool pollArt();
  // The artwork chooser: what the sources offered, and the picks so far.
  ArtFetch::Choices artChoices;
  int artPick[3] = {-1, -1, -1}, artRow = 0;
  void finishChoosing(bool save);
  // Launcher preferences, kept in launcher-preferences.json.
  bool blurAdult = true;
  void savePreferences();
  // Filter rows: engines, favourites, unplayed, then sorts.
  int filterRows() const;
  void filterActivate(int row);
  // Hit boxes the view records while drawing, so touch matches what is shown.
  mutable int filterHit[16][4] = {};
  struct HintHit {
    int x, w;
    Button button;
  };
  mutable std::vector<HintHit> hintHit;
  mutable int gridScroll = 0;
  // Engine tab boxes, left edge and width, as last drawn.
  // Drawn engine tabs: left edge, width and engine, for touch.
  struct TabHit {
    int x, w, engine;
  };
  mutable std::vector<TabHit> tabHit;
  // Set by the host each frame: "21:14" and 0-100, or -1 when unknown.
  std::string clockText;
  // Frame timing readout for testing, toggled by tapping the HardRPG logo.
  bool showFrameTime = false;
  std::string frameText;
  int batteryPercent = -1;

  int setting = 0, settingFirst = 0, rtpSelected = 0;
  int engineFilter = 0, aboutScroll = 0;
  std::string query, launchingName, preparationLabel;
  // A step-by-step job before launch (0..1); below 0 when there is none.
  double preparationFraction = -1;
  std::vector<ErrorRecord> errors;
  int rtpPending = -1;
  std::shared_ptr<RtpCheck> rtpCheck;
  unsigned rtpGeneration = 0;
  int browserSelected = 0, browserFirst = 0, errorScroll = 0;
  Focus focus = Focus::Home;
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
  // Opens the game settings of model.list[index].
  void openGameMenu(size_t index);
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
  bool service();
  void search(const std::string &);
  void touch(int x, int y);
  // Touch on the sheets (every screen but home).
  void sheetTouch(int x, int y);
  // Rows on the settings page; the last is Exit.
  int settingsRows() const { return 8; }
  // The game info page (Page::Keys): the adult artwork blur, then the source
  // keys, typed on the Vita keyboard.
  static constexpr int keyCount = 2;
  static constexpr int keysRows = 1 + keyCount;
  std::string keyFile(int index) const;
  std::string readKey(int index) const;
  void editKey(int index);
  // Asks for text on the system keyboard: heading, initial text, result.
  std::function<bool(const std::string &, const std::string &, std::string &)> enterText;
  void scroll(int rows);
  void openPage(Page);
  void refreshRtp();
  void refreshFolders();

private:
  void openBrowser(BrowseMode);
  void browserAction(Button);
  // Plays model.list[index], or opens it when it is a collection.
  void play(size_t index);
  void openSetting();
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
