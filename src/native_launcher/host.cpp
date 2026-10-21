// Desktop preview and fixture driver for the production native model/view.
#include "runtime.h"
#include "view.h"
#include "release.h"
#include <SDL.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <thread>

namespace {
using namespace hardrpg;
Json array() { return Json(std::initializer_list<Json>{}); }
Json object() { return Json(std::initializer_list<Json::pair_type>{}); }
std::string field(const Json &v, const std::string &key,
                  const std::string &fallback = "") {
  auto i = v.as_object().find(key);
  return i == v.as_object().end() ? fallback : i->second.as_string();
}
Button button(const std::string &b) {
  const char *names[] = {"none",   "up",       "down", "cross", "circle",
                         "square", "triangle", "left", "right", "start",
                         "select", "l",        "r",    "paths"};
  const Button buttons[] = {Button::None,     Button::Up,     Button::Down,
                            Button::Cross,    Button::Circle, Button::Square,
                            Button::Triangle, Button::Left,   Button::Right,
                            Button::Start,    Button::Select, Button::L,
                            Button::R,        Button::Paths};
  for (int i = 0; i < 14; ++i)
    if (b == names[i])
      return buttons[i];
  throw std::runtime_error("Unknown preview action");
}
Json listing(const std::vector<Entry> &list, const Paths &paths) {
  auto out = array();
  for (auto &e : list) {
    auto item = object();
    auto &o = item.as_object();
    o["name"] = e.name;
    o["kind"] = e.kind == Kind::Game  ? "game"
                : e.kind == Kind::Zip ? "zip"
                                      : "folder";
    o["path"] = e.node.path();
    o["inner"] = e.node.inner;
    o["config"] = key(e.node.identity(paths));
    o["rgss"] = e.version <= 3 ? e.version : 0;
    if (e.version == 4) {
      o["format"] = e.version;
      o["backend"] = engineFor(e.version)->id;
    }
    out.as_array().push_back(item);
  }
  return out;
}
Json commands(Model &m, const Json &script) {
  auto out = array();
  for (auto &cmd : script.as_array()) {
    auto result = object();
    try {
      auto op = field(cmd, "op"), path = field(cmd, "path");
      bool zip = field(cmd, "kind") == "zip";
      Node node;
      if (!path.empty())
        node = Node::fromPath(m.paths, path, zip, field(cmd, "inner"));
      auto cancel = field(cmd, "cancel");
      auto progress = [&](uint64_t, uint64_t) {
        if (cancel == "yes")
          throw Cancelled("ZIP preparation cancelled.");
      };
      auto scan = [&](size_t, const std::string &) {
        if (cancel == "yes")
          throw Cancelled("Folder scan cancelled.");
      };
      if (op == "list")
        result = listing(path.empty() ? m.list : entries(node), m.paths);
      else if (op == "game") {
        Entry e;
        if (!game(node, e))
          throw std::runtime_error("Not a game");
        result = listing({e}, m.paths);
      } else if (op == "launch") {
        Entry e;
        if (!game(node, e))
          throw std::runtime_error("Not a game");
        m.backendAvailable = field(cmd, "runtime") == "missing"
            ? std::function<bool(const Engine &)>([](const Engine &) { return false; })
            : std::function<bool(const Engine &)>{};
        result = m.launch(e, progress);
      } else if (op == "add")
        result = Json::integer_type(m.addFolder(path, scan));
      else if (op == "add-search")
        result = Json::integer_type(m.addSearchFolder(path, scan));
      else if (op == "remove-search") {
        m.removeSearchFolder(path);
        result = true;
      } else if (op == "refresh") {
        m.refresh();
        result = listing(m.list, m.paths);
      } else if (op == "search-folders") {
        result = array();
        for (auto &p : m.searchFolders())
          result.as_array().push_back(p);
      } else if (op == "set-rtp") {
        m.setRtp(field(cmd, "pack"), path);
        result = m.rtpPaths();
      } else if (op == "rtp-status")
        result = m.rtpStatus(field(cmd, "pack"));
      else if (op == "display")
        result = m.display();
      else if (op == "set-display") {
        m.changeDisplay(field(cmd, "key"), cmd.as_object().at("value"));
        result = m.display();
      } else if (op == "error") {
        result = array();
        for (auto &s : m.takeError())
          result.as_array().push_back(s);
      } else if (op == "rename") {
        Entry e;
        if (!game(node, e)) {
          if (!zip)
            throw std::runtime_error("Not a game");
          e = {path, Kind::Zip, node};
        }
        m.setDisplayName(e, field(cmd, "name"));
        result = listing(m.list, m.paths);
      } else
        throw std::runtime_error("Unknown native model command");
    } catch (const MissingRtp &e) {
      result.as_object()["error"] = e.what();
      result.as_object()["pack"] = e.pack;
    } catch (const std::exception &e) {
      result = object();
      result.as_object()["error"] = e.what();
    }
    out.as_array().push_back(result);
  }
  return out;
}
void preview(UI &ui, View &view) {
  if (SDL_Init(SDL_INIT_VIDEO))
    throw std::runtime_error(SDL_GetError());
  SDL_Window *window = SDL_CreateWindow("HardRPG native launcher preview",
                                        SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, 960, 544, 0);
  if (!window) {
    SDL_Quit();
    throw std::runtime_error(SDL_GetError());
  }
  SDL_Surface *frame =
      SDL_CreateRGBSurfaceFrom(view.pixels.data(), 960, 544, 32, 960 * 4, 0xff,
                               0xff00, 0xff0000, 0xff000000);
  if (!frame) {
    SDL_DestroyWindow(window);
    SDL_Quit();
    throw std::runtime_error(SDL_GetError());
  }
  auto draw = [&] {
    std::time_t now = std::time(nullptr);
    char clock[8];
    std::strftime(clock, sizeof clock, "%H:%M", std::localtime(&now));
    ui.clockText = clock;
    view.draw(ui);
    SDL_BlitSurface(frame, nullptr, SDL_GetWindowSurface(window), nullptr);
    SDL_UpdateWindowSurface(window);
  };
  draw();
  bool quit = false;
  ui.progressPump = [&] {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        quit = true;
      if (quit || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
        throw Cancelled("Preparation cancelled.");
    }
    draw();
  };
  uint32_t lastTick = SDL_GetTicks();
  while (!quit && !ui.exitRequested && !ui.launchRequested) {
    SDL_Event e;
    bool moving = ui.animating() || view.animating();
    if (!SDL_WaitEventTimeout(&e, moving ? 16 : 50)) {
      uint32_t now = SDL_GetTicks();
      if (moving) {
        ui.tick((now - lastTick) / 1000.0);
        draw();
      }
      lastTick = now;
      continue;
    }
    lastTick = SDL_GetTicks();
    if (e.type == SDL_QUIT)
      quit = true;
    // The mouse stands in for the front touch screen.
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP ||
        (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK))) {
      int state = e.type == SDL_MOUSEBUTTONDOWN ? 0 : e.type == SDL_MOUSEMOTION ? 1 : 2;
      int x = state == 1 ? e.motion.x : e.button.x, y = state == 1 ? e.motion.y : e.button.y;
      if (ui.homeActive() || state == 2) {
        if (ui.homeActive())
          ui.homePointer(state, x, y, SDL_GetTicks());
        else
          ui.touch(x, y);
        while (ui.service()) {
        }
        draw();
      }
      continue;
    }
    if (e.type == SDL_KEYDOWN) {
      Button b = Button::None;
      switch (e.key.keysym.sym) {
      case SDLK_UP:
        b = Button::Up;
        break;
      case SDLK_DOWN:
        b = Button::Down;
        break;
      case SDLK_RETURN:
        b = Button::Cross;
        break;
      case SDLK_ESCAPE:
        b = Button::Circle;
        break;
      case SDLK_SPACE:
        b = Button::Square;
        break;
      case SDLK_r:
        b = Button::Triangle;
        break;
      case SDLK_LEFT:
        b = Button::Left;
        break;
      case SDLK_RIGHT:
        b = Button::Right;
        break;
      case SDLK_f:
        b = Button::Select;
        break;
      case SDLK_q:
        b = Button::L;
        break;
      case SDLK_e:
        b = Button::R;
        break;
      case SDLK_TAB:
        b = Button::Start;
        break;
      }
      ui.action(b);
      while (ui.service()) {
      }
      draw();
    }
  }
  SDL_FreeSurface(frame);
  SDL_DestroyWindow(window);
  SDL_Quit();
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc > 1 && std::string(argv[1]) == "--boot-check") {
      std::cout << (nativeLauncherRequested(argc - 1, argv + 1) ? "true"
                                                                : "false")
                << "\n";
      return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--runtime-smoke") {
      if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER |
                   SDL_INIT_EVENTS))
        throw std::runtime_error(SDL_GetError());
      SDL_AddTimer(
          1000,
          [](Uint32, void *) -> Uint32 {
            SDL_Event event{};
            event.type = SDL_QUIT;
            SDL_PushEvent(&event);
            return 0;
          },
          nullptr);
      int rc = runNativeLauncher(true);
      SDL_Quit();
      return rc;
    }
    hardrpg::Paths paths;
    std::string font = "assets/liberation.ttf", snapshot, script, actions, releaseFixture,
                rename, search, text;
    bool edit = false, settle = false, returning = false;
    bool interactive = false, storageSet = false;
    int battery = 78;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--interactive") {
        interactive = true;
        continue;
      }
      if (i + 1 == argc)
        throw std::runtime_error("Missing option value");
      std::string value = argv[++i];
      if (arg == "--search")
        search = value;
      else if (arg == "--root")
        paths.root = value;
      else if (arg == "--storage") {
        if (!storageSet)
          paths.storage.clear();
        storageSet = true;
        paths.storage.push_back(value);
      } else if (arg == "--font")
        font = value;
      else if (arg == "--release-fixture")
        releaseFixture = value;
      else if (arg == "--snapshot")
        snapshot = value;
      else if (arg == "--commands")
        script = value;
      else if (arg == "--actions")
        actions = value;
      else if (arg == "--battery")
        battery = std::stoi(value);
      else if (arg == "--rename") {
        rename = value;
        edit = true;
      } else if (arg == "--text")
        text = value;
      else if (arg == "--returning") {
        settle = true;
        returning = value == "yes";
      }
      else
        throw std::runtime_error("Unknown preview option");
    }
    if (!script.empty()) {
      hardrpg::Model model(paths);
      std::cout << commands(model,
                            json5pp::parse(hardrpg::readFile(script, 1048576)))
                       .stringify()
                << "\n";
      return 0;
    }
    hardrpg::UI ui(paths);
    if (settle) {
      // As the Vita does at boot: "yes" after a game handed back, "no" after
      // a fresh start.
      ui.model.settleActivity(returning);
      ui.loadHome();
    }
    ui.batteryPercent = battery;
    if (!text.empty())
      ui.enterText = [&](const std::string &, const std::string &, std::string &out) {
        out = text;
        return text != "--cancel";
      };
    if (edit)
      ui.editName = [&](const std::string &, std::string &out) {
        out = rename;
        return rename != "--cancel";
      };
    ui.search(search);
    if(!releaseFixture.empty())ui.offerRelease(newerRelease(readFile(releaseFixture,262144),HARDRPG_VERSION_TAG));
    hardrpg::View view(font);
    std::istringstream stream(actions);
    std::string action;
    while (std::getline(stream, action, ','))
      if (!action.empty()) {
        if (action.find("touch:") == 0) {
          int x, y;
          if (std::sscanf(action.c_str(), "touch:%d:%d", &x, &y) == 2)
            ui.touch(x, y);
        } else if (action.find("drag:") == 0) {
          // drag:x1:x2:y  a horizontal swipe on the front screen, 200 ms long.
          int x1, x2, y;
          if (std::sscanf(action.c_str(), "drag:%d:%d:%d", &x1, &x2, &y) == 3) {
            ui.homePointer(0, x1, y, 0);
            for (int step = 1; step <= 10; ++step)
              ui.homePointer(1, x1 + (x2 - x1) * step / 10, y, step * 20);
            ui.homePointer(2, x2, y, 200);
          }
        } else if (action.find("sample:") == 0) {
          // sample:BUTTONS:LX:LY:RX:RY[:FX:FY]  what the controls test would read
          // from the Vita: a button bit mask, sticks, and one front touch.
          unsigned buttons;
          int lx, ly, rx, ry;
          float fx = -1, fy = -1;
          if (std::sscanf(action.c_str(), "sample:%u:%d:%d:%d:%d:%f:%f", &buttons, &lx, &ly, &rx, &ry, &fx, &fy) >= 5) {
            ui.controlsSample.buttons = uint16_t(buttons);
            ui.controlsSample.lx = lx, ui.controlsSample.ly = ly, ui.controlsSample.rx = rx, ui.controlsSample.ry = ry;
            if (fx >= 0)
              ui.controlsSample.front = {{0, fx, fy}};
          }
        } else if (action == "wait-art") {
          // Let an artwork download finish (up to two minutes).
          for (int i = 0; i < 1200 && ui.artFetch && ui.artFetch->running() && ui.home.panel != Home::Artwork; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ui.pollArt();
          }
          ui.pollArt();
        } else if (action.find("scroll:") == 0)
          ui.scroll(std::stoi(action.substr(7)));
        else if (action == "loadexec-fail")
          ui.launchFailed(-2139086818, "app0:/easyrpg.self");
        else
          ui.action(button(action));
      }
    while (ui.service()) {
    }
    for (int frame = 0; frame < 600 && ui.animating(); ++frame)
      ui.tick(1 / 60.0);
    {
      std::time_t now = std::time(nullptr);
      char clock[8];
      std::strftime(clock, sizeof clock, "%H:%M", std::localtime(&now));
      ui.clockText = clock;
    }
    if (!snapshot.empty()) {
      // Let artwork finish loading on its thread, then end fades.
      view.draw(ui);
      auto start = std::chrono::steady_clock::now();
      while (view.pendingLoads && std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        view.draw(ui);
      }
      view.settle();
      view.draw(ui);
      view.save(snapshot);
    }
    if (interactive)
      preview(ui, view);
    auto state = object();
    state.as_object()["entries"] = listing(ui.model.list, paths);
    std::vector<Entry> visible;
    for (auto index : ui.visible())
      visible.push_back(ui.model.list[index]);
    state.as_object()["visibleEntries"] = listing(visible, paths);
    {
      // Home: its games in the order shown, and where the focus is.
      std::vector<Entry> shown;
      for (auto index : ui.homeList())
        shown.push_back(ui.model.list[index]);
      auto &o = state.as_object();
      o["homeEntries"] = listing(shown, paths);
      o["homeActive"] = ui.homeActive();
      o["homeTarget"] = ui.home.target;
      o["homeZone"] = int(ui.home.zone);
      o["homePanel"] = int(ui.home.panel);
      o["homeGrid"] = ui.home.grid;
      o["homeDetails"] = ui.home.details;
      o["homeSort"] = ui.home.sort;
      o["homeDirection"] = ui.home.direction;
      o["blurAdult"] = ui.blurAdult;
    }
    state.as_object()["query"] = ui.query;
    state.as_object()["engineFilter"] = ui.engineFilter;
    auto errors = array();
    for (auto &record : ui.errors) {
      auto e = object();
      e.as_object()["game"] = record.game;
      e.as_object()["timestamp"] = record.timestamp;
      errors.as_array().push_back(e);
    }
    state.as_object()["errors"] = errors;
    state.as_object()["message"] = ui.message;
    state.as_object()["launchRequested"] = ui.launchRequested;
    state.as_object()["launchFormat"] = ui.launchFormat;
    state.as_object()["launchingName"] = ui.launchingName;
    state.as_object()["preparationLabel"] = ui.preparationLabel;
    state.as_object()["rtpSelected"] = ui.rtpSelected;
    state.as_object()["missingRtp"] = ui.missingRtp;
    state.as_object()["page"] = int(ui.page);
    state.as_object()["releaseLines"] = int(ui.releaseReport.size());
    state.as_object()["releaseScroll"] = ui.releaseScroll;
    state.as_object()["releaseTag"] = ui.releaseNotice.tag;
    state.as_object()["controls"] = ui.controls.json();
    state.as_object()["controlsTesting"] = ui.controlsTesting;
    state.as_object()["controlPicking"] = ui.controlPicking;
    state.as_object()["gameOptions"] = ui.gameOptions;
    state.as_object()["configuredGame"] = ui.gameOptions ? ui.gameEntry.name : "";
    state.as_object()["gameConfigKey"] = ui.gameOptions ? key(ui.gameEntry.node.identity(paths)) : "";
    state.as_object()["gameSettings"] = ui.gameOptions ? ui.model.gameSettings(ui.gameEntry) : object();
    state.as_object()["setting"] = ui.setting;
    {
      auto rows = array();
      if (ui.page == Page::Remove)
        rows.as_array().push_back(ui.removePath(ui.removeQuestions[ui.removeStep]));
      state.as_object()["removeQuestion"] = rows;
    }
    state.as_object()["settingFirst"] = ui.settingFirst;
    state.as_object()["automaticUpdates"] = ui.automaticUpdates;
    state.as_object()["focus"] = int(ui.focus);
    state.as_object()["errorScroll"] = ui.errorScroll;
    state.as_object()["errorLines"] = int(ui.errorReport.size());
    state.as_object()["exitRequested"] = ui.exitRequested;
    state.as_object()["browserOpen"] = bool(ui.browser);
    state.as_object()["browserPath"] = ui.browser ? ui.browser->path : "";
    std::cout << state.stringify() << "\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
