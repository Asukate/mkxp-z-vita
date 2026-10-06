#pragma once
#include "model.h"
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
enum class Page { Home, Display, Rtp, Folders, Errors };
enum class BrowseMode { Games, Rtp, Folder };
// This state machine is shared by the Vita front end and the desktop preview.
// It owns no Ruby values, RGSS objects, audio devices, or engine threads.
class UI {
public:
  explicit UI(Paths paths = {});
  Model model;
  int menu = 0, selected = 0, first = 0, setting = 0, settingFirst = 0,
      rtpSelected = 0;
  int engineFilter = 0, aboutScroll = 0, sourceScroll = 0;
  std::string query, launchingName;
  std::vector<ErrorRecord> errors;
  std::vector<std::string> sources;
  int rtpPending = -1;
  int browserSelected = 0, browserFirst = 0, errorScroll = 0;
  Focus focus = Focus::Menu;
  Page page = Page::Home;
  BrowseMode browseMode = BrowseMode::Games;
  std::unique_ptr<Browser> browser;
  std::string message, missingRtp;
  std::vector<std::string> errorReport;
  Json displayValues, rtpChoices;
  std::vector<std::string> gameFolders;
  std::vector<std::string> rtpStatuses;
  std::vector<std::pair<int, int>> history;
  bool launchRequested = false, exitRequested = false;
  std::function<void()> progressPump;
  // Returning false cancels without changing the persistent display name.
  std::function<bool(const std::string &, std::string &)> editName;
  std::function<bool(const std::string &, std::string &)> searchText;
  std::function<void()> beginLaunch;
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
  void progress(uint64_t, uint64_t);
  void scanProgress(size_t, const std::string &);
  void chooseRtp(const std::string &);
};
} // namespace hardrpg
