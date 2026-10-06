#include "ui.h"
#include "release.h"
#include <algorithm>

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
  errorReport = model.takeError();
  if (errorReport.empty())
    model.takeError(true);
}
void UI::resetSelection() {
  selected = first = 0;
  message.clear();
}
void UI::refreshRtp() {
  rtpChoices = model.rtpPaths();
  rtpStatuses.assign(3, "Checking...");
  rtpPending = 0;
}
bool UI::service() {
  if (rtpPending < 0)
    return false;
  if (page != Page::Rtp) {
    rtpPending = -1;
    return false;
  }
  try {
    rtpStatuses[rtpPending] = model.rtpStatus(packs[rtpPending]);
  } catch (const std::exception &e) {
    rtpStatuses[rtpPending] = "Unavailable";
    message = e.what();
  }
  if (++rtpPending == 3)
    rtpPending = -1;
  return true;
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
  if (browser || !errorReport.empty() || !missingRtp.empty())
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
      rtpSelected = std::max(0, std::min(rtpSelected + rows, 2));
      return;
    }
    int count = page == Page::Errors    ? errors.size()
                : page == Page::Folders ? 1 + gameFolders.size()
                : page == Page::Home    ? 4
                                        : 3;
    setting = std::max(0, std::min(setting + rows, std::max(0, count - 1)));
    settingFirst = std::max(0, std::min(settingFirst, setting));
    if (setting >= settingFirst + 7)
      settingFirst = setting - 6;
  }
}
void UI::touch(int x, int y) {
  if (browser || !errorReport.empty() || !missingRtp.empty())
    return;
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
    int row = (y - top) / (page == Page::Rtp       ? 86
                           : page == Page::Folders ? 36
                                                   : 42);
    int count = page == Page::Home      ? 4
                : page == Page::Display ? 3
                : page == Page::Rtp     ? 3
                : page == Page::Folders ? 1 + gameFolders.size()
                                        : errors.size();
    if (y >= top && row < (page == Page::Rtp ? 3 : 7) &&
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
void UI::refreshFolders() { gameFolders = model.searchFolders(); }
void UI::openPage(Page next) {
  if (next == Page::Display)
    displayValues = model.display();
  if (next == Page::Rtp)
    refreshRtp();
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
  model.changeDisplay(k, v);
  displayValues = model.display();
  message = "Display settings saved for the next game launch.";
}
void UI::progress(uint64_t done, uint64_t total) {
  message =
      "Preparing ZIP: " + std::to_string(total ? done * 100 / total : 100) +
      "% - Circle: cancel";
  if (progressPump)
    progressPump();
}
void UI::scanProgress(size_t count, const std::string &) {
  message = "Scanning folder " + std::to_string(count) + " - Circle: cancel";
  if (progressPump)
    progressPump();
}
void UI::chooseRtp(const std::string &path) {
  model.setRtp(packs[rtpSelected], path);
  browser.reset();
  menu = 1;
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
    if (page == Page::Home) {
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
  if (model.enter(e, chosen)) {
    model.launch(chosen, [this](uint64_t done, uint64_t total) {
      progress(done, total);
    });
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
        for (int i = 0; i < 3; ++i)
          if (missingRtp == packs[i])
            rtpSelected = i;
        openPage(Page::Rtp);
        menu = 1;
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
      engineFilter = (engineFilter + (b == Button::R ? 1 : 3)) % 4;
      selected = first = 0;
      return;
    }
    if (b == Button::Up || b == Button::Down) {
      int step = b == Button::Up ? -1 : 1;
      if (focus == Focus::Menu)
        menu = (menu + step + 4) % 4;
      else if (focus == Focus::Settings) {
        if (page == Page::Rtp)
          rtpSelected = (rtpSelected + step + 3) % 3;
        else
          move(setting, settingFirst,
               page == Page::Errors    ? errors.size()
               : page == Page::Folders ? 1 + gameFolders.size()
               : page == Page::Home    ? 4
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
      if (!editName)
        throw std::runtime_error("Game name keyboard is unavailable.");
      std::string name;
      if (!editName(e.name, name))
        return;
      auto id = e.node.identity(model.paths);
      model.setDisplayName(e, name);
      auto shown = visible();
      selected = first = 0;
      for (size_t i = 0; i < shown.size(); ++i)
        if (model.list[shown[i]].node.identity(model.paths) == id) {
          selected = i;
          first = std::max(0, std::min(first, selected));
          if (selected >= first + gameRows())
            first = selected - gameRows() + 1;
          break;
        }
      message = name.empty() ? "Original game name restored."
                             : "Display name saved. Game files stay unchanged.";
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
        page == Page::Display) {
      changeDisplay(b == Button::Left ? -1 : 1);
      return;
    }
    if (b == Button::Triangle) {
      if (focus == Focus::Settings && page == Page::Rtp) {
        model.clearRtpCache();
        refreshRtp();
        message = "Checking RTP locations...";
      } else if (focus == Focus::Settings && page == Page::Folders) {
        model.refresh();
        refreshFolders();
        message = "Game folders refreshed.";
      } else if (menu == 0) {
        model.refresh();
        sources = model.libraryRoots();
        resetSelection();
      }
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
