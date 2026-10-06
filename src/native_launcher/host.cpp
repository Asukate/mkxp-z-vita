// Desktop preview and fixture driver for the production native model/view.
#include "runtime.h"
#include "view.h"
#include <SDL.h>
#include <iostream>
#include <sstream>

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
    o["rgss"] = e.version;
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
  while (!quit && !ui.exitRequested && !ui.launchRequested) {
    SDL_Event e;
    if (!SDL_WaitEventTimeout(&e, 50))
      continue;
    if (e.type == SDL_QUIT)
      quit = true;
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
    std::string font = "assets/liberation.ttf", snapshot, script, actions,
                rename, search;
    bool edit = false;
    bool interactive = false, storageSet = false;
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
      else if (arg == "--snapshot")
        snapshot = value;
      else if (arg == "--commands")
        script = value;
      else if (arg == "--actions")
        actions = value;
      else if (arg == "--rename") {
        rename = value;
        edit = true;
      } else
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
    if (edit)
      ui.editName = [&](const std::string &, std::string &out) {
        out = rename;
        return rename != "--cancel";
      };
    ui.search(search);
    hardrpg::View view(font);
    std::istringstream stream(actions);
    std::string action;
    while (std::getline(stream, action, ','))
      if (!action.empty()) {
        if (action.find("touch:") == 0) {
          int x, y;
          if (std::sscanf(action.c_str(), "touch:%d:%d", &x, &y) == 2)
            ui.touch(x, y);
        } else if (action.find("scroll:") == 0)
          ui.scroll(std::stoi(action.substr(7)));
        else
          ui.action(button(action));
      }
    while (ui.service()) {
    }
    if (!snapshot.empty()) {
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
    state.as_object()["menu"] = ui.menu;
    state.as_object()["launchRequested"] = ui.launchRequested;
    state.as_object()["missingRtp"] = ui.missingRtp;
    state.as_object()["page"] = int(ui.page);
    state.as_object()["focus"] = int(ui.focus);
    state.as_object()["selected"] = ui.selected;
    state.as_object()["first"] = ui.first;
    state.as_object()["errorScroll"] = ui.errorScroll;
    state.as_object()["errorLines"] = int(ui.errorReport.size());
    state.as_object()["exitRequested"] = ui.exitRequested;
    state.as_object()["browserPath"] = ui.browser ? ui.browser->path : "";
    std::cout << state.stringify() << "\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
