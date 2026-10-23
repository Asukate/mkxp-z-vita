#pragma once
#include "art_fetch.h"
#include "i18n.h"
#include "model.h"
#include "sounds.h"
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
  Keys, // artwork source keys
  Sound
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
  // In the details panel: the focused screenshot (-1 none), and the one
  // shown full screen (-1 none).
  int shotFocus = -1, viewing = -1;
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
// A control mapping's name in the launcher's language.
inline std::string controlText(int code) {
  if (controlIsKey(code)) {
    int key = controlPickIndex(code);
    return key >= 0 ? trf("Key {}", {controlPicks[key].name}) : tr("Off");
  }
  return tr(controlName(code));
}
// A Vita button's name (controlSources order); SELECT and START keep theirs.
inline std::string buttonText(int index) {
  return index == 4 || index == 5 ? controlSources[index] : tr(controlSources[index]);
}
// The token that draws a Vita button's icon in text (controlSources order).
inline const char *buttonIcon(int index) {
  static const char *const tokens[] = {"<cross>", "<circle>", "<square>", "<triangle>", "<select>", "<start>",
                                       "<up>",    "<down>",   "<left>",   "<right>",    "<l>",      "<r>"};
  return index >= 0 && index < 12 ? tokens[index] : "";
}
// The welcome guide shown on the first start: language, where games go, the
// RTP, the library view, a few defaults, game info. Cross moves on, Circle
// goes back.
struct Welcome {
  enum Slide { Language, Games, Rtp, View, Defaults, Artwork, SlideCount };
  bool active = false;
  int slide = 0, from = -1; // from: the slide sliding out, or -1
  int direction = 1;        // 1 forward, -1 back
  double amount = 1;        // 0 when a slide change starts, 1 once it is done
  double clock = 0;         // seconds since the slide came in, for its own animations
  int languageFocus = 0;
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
  // Game info for every game not looked up before, in the background and
  // without messages. A download the player asks for stops it.
  bool autoGameInfo = true;
  void fetchArtAutomatically();
  bool artQuiet = false;
  std::vector<std::string> artQuietKeys;
  // The artwork chooser: what the sources offered, and the picks so far.
  ArtFetch::Choices artChoices;
  int artPick[3] = {-1, -1, -1}, artRow = 0;
  void finishChoosing(bool save);
  bool lastArtRow() const; // Cross saves here instead of going to the next row
  // Launcher preferences, kept in launcher-preferences.json: the adult
  // artwork blur, the language and whether the welcome guide was seen.
  bool blurAdult = false; // off unless chosen in Settings
  bool welcomeSeen = false;
  // Sound: interface sounds and each game's title music while it is
  // highlighted, with their levels (0-100).
  bool menuSounds = true, themeMusic = true;
  int effectsVolume = 70, musicVolume = 80;
  void savePreferences();
  // Plays interface sounds; the front end sets it. Unset plays nothing.
  std::function<void(Sound)> sound;
  void emit(Sound) const;
  // Where the title music of the game to play now comes from (its folder,
  // unpacked copy or ZIP), or "".
  std::string themeFolder() const;
  // The same for one game, then for every game listed and for the games on
  // either side of the highlighted one (made ready in the background).
  std::string themeSource(const Entry &) const;
  // Where each ZIP game's music is read from, found once per library load:
  // the home loop asks every 100 ms, and a memory card check can wait
  // behind the artwork loader for a long time.
  mutable std::map<std::string, std::string> themeSourceCache;
  // Title music the player put beside a game's pictures (art/<folder>/theme.*),
  // by entry key, as the picture loader finds it.
  mutable std::map<std::string, std::string> customThemes;
  std::vector<std::string> themeSources() const;
  std::vector<std::string> themeNearby() const;
  // RTP folders set in Settings, where a game's title track may be.
  std::vector<std::string> rtpFolders() const;
  // Nesting of input handlers, so one input makes one sound.
  int feedbackDepth = 0;
  void changeLanguage(int step);
  void selectLanguage(int lang);
  // The welcome guide (see Welcome).
  Welcome welcome;
  void openWelcome();
  void welcomeAction(Button);
  void welcomeTap(int x, int y);
  void finishWelcome();
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
  // Screenshots in the details panel as last drawn: how many, and where.
  mutable int shotsShown = 0;
  mutable int shotHit[4][4] = {};
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
  int gameOptionsCount() const { return inGame ? 5 : canFreeZip ? 10 : 9; }
  // The menu opened over a running game (L+R+Start or the corner icon): the
  // game's page with Resume, Controls, Display, Performance and speed and
  // Back to library. Its changes apply at once (see inGameChanged).
  bool inGame = false;
  enum InGameExit { Staying, Resume, BackToLibrary };
  InGameExit inGameExit = Staying;
  void openInGame(const Entry &);
  // Set when a setting changed, so the game applies it; the host clears it.
  bool inGameChanged = false;
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
  // The pad page draws the console (front, or its back for the rear pad)
  // and each zone where it really is on the surface. These are the surface
  // (the screen in the picture, or the rear pad), and the example game
  // picture the drawn buttons sit in (4:3, centred; it varies by game).
  static constexpr int padArtX = 200, padArtY = 210, padArtW = 540;
  static void padSurface(int &x, int &y, int &w, int &h, bool rear = false) {
    if (rear) {
      // The patterned touch area of ui/vita-rear-640.png (640x270).
      const double k = padArtW / 640.0;
      x = padArtX + int(131 * k + .5);
      y = padArtY + int(41 * k + .5);
      w = int((509 - 131) * k + .5);
      h = int((192 - 41) * k + .5);
      return;
    }
    const double k = padArtW / 1800.0;
    x = padArtX + int(356 * k + .5);
    y = padArtY + int(66 * k + .5);
    w = int((1443 - 356) * k + .5);
    h = int((673 - 66) * k + .5);
  }
  static void padGamePicture(int &x, int &y, int &w, int &h) {
    padSurface(x, y, w, h);
    int gw = h * 4 / 3;
    x += (w - gw) / 2;
    w = gw;
  }
  // Where each row of the pad page is drawn and touched: the mode bar, then
  // each zone at its place on the surface, left and right as seen when
  // holding the console.
  void padRowRect(int row, int &x, int &y, int &w, int &h) const {
    if (row == 0) {
      x = 184; y = 151; w = 744; h = 35;
      return;
    }
    bool rear = controlsPad == 2;
    int mode = rear ? controls.rear : controls.front;
    int zone = padZone(row);
    unsigned mask = padMask();
    padSurface(x, y, w, h, rear);
    if (zone < 0 || mask == 1)
      return;
    auto part = [&](double fx, double fy, double fw, double fh) {
      int sx = x, sy = y, sw = w, sh = h;
      x = sx + int(sw * fx + .5);
      y = sy + int(sh * fy + .5);
      w = int(sw * fw + .5);
      h = int(sh * fh + .5);
    };
    if (!rear && (mode == 1 || mode == 3)) {
      // Drawn buttons: the bottom corners of the game picture.
      padGamePicture(x, y, w, h);
      part(zone % 2 ? .75 : 0, .8, .25, .2);
      return;
    }
    if (rear && mode == 1) {
      // Two zones along the top edge of the rear pad.
      part(zone % 2 ? .65 : 0, 0, .35, .35);
      return;
    }
    if (mask == 15) {
      part(zone % 2 ? .5 : 0, zone >= 2 ? .5 : 0, .5, .5);
      return;
    }
    part(zone % 2 ? .5 : 0, 0, .5, 1);
  }
  // The controls page's main view is a drawing of the Vita: each button's
  // mapping in a label beside it, with a line to the button; the screen in
  // the drawing opens the screen's touch zones; the other rows are pills
  // along the bottom. Rows keep their numbers (0 custom controls, 1-12 the
  // buttons in controlSources order, 13 tap to walk, 14 front screen, 15
  // rear pad, 16 sticks, 17 test, 18 reset); only where they are changes.
  bool controlsMap() const {
    return page == Page::Controls && !controlsAdvanced && !controlsPad && !controlPicking && !controlsTesting;
  }
  static constexpr int mapArtX = 225, mapArtY = 176, mapArtW = 510;
  // Where row is drawn and touched.
  static bool controlsRect(int row, int &x, int &y, int &w, int &h);
  // The button a row's label points at, on the drawing.
  static bool controlsAnchor(int row, int &x, int &y);
  // The row the D-pad goes to from row: the nearest one that way.
  static int controlsNeighbor(int row, Button);
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
  // A touch zone's name on the pad page ("Top left").
  std::string zoneName(int row) const;
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
  int settingsRows() const { return 11; }
  // Rows on the Sound page.
  static constexpr int soundRows = 4;
  void changeSound(int step, bool reset = false);
  // The game info page (Page::Keys): the adult artwork blur, then the source
  // keys, typed on the Vita keyboard.
  static constexpr int keyCount = 4;
  static constexpr int keysRows = 2 + keyCount;
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
