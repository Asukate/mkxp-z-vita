#include "game_menu.h"
#include "i18n.h"
#include "ui.h"
#include "view.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifdef __vita__
#include "../input/vita_input.h"
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#endif

namespace hardrpg {
namespace {
uint32_t millis() {
#ifdef __vita__
  return uint32_t(sceKernelGetProcessTimeWide() / 1000);
#else
  return 0;
#endif
}
} // namespace

struct GameMenu::Impl {
  std::unique_ptr<UI> ui;
  std::unique_ptr<View> view;
  bool dirty = true;
  uint32_t drawnAt = 0;
  // Buttons: nothing counts until every button is up (the menu opens while
  // L+R+Start are still held); then presses, and repeats for the D-pad.
  bool armed = false;
  uint32_t previous = 0, repeatAt = 0;
  // Front screen: a tap is a press and release that did not drag.
  bool touching = false, dragged = false;
  int touchX = 0, touchY = 0, dragY = 0;

  Button button() {
#ifdef __vita__
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t held = pad.buttons;
    if (pad.lx < 80)
      held |= SCE_CTRL_LEFT;
    else if (pad.lx > 176)
      held |= SCE_CTRL_RIGHT;
    if (pad.ly < 80)
      held |= SCE_CTRL_UP;
    else if (pad.ly > 176)
      held |= SCE_CTRL_DOWN;
    static const uint32_t masks[] = {SCE_CTRL_UP,     SCE_CTRL_DOWN,     SCE_CTRL_CROSS,    SCE_CTRL_CIRCLE,
                                     SCE_CTRL_SQUARE, SCE_CTRL_TRIANGLE, SCE_CTRL_LEFT,     SCE_CTRL_RIGHT,
                                     SCE_CTRL_START,  SCE_CTRL_SELECT,   SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER};
    static const Button buttons[] = {Button::Up,     Button::Down,     Button::Cross, Button::Circle,
                                     Button::Square, Button::Triangle, Button::Left,  Button::Right,
                                     Button::Start,  Button::Select,   Button::L,     Button::R};
    if (!armed) {
      previous = held;
      armed = !held;
      return Button::None;
    }
    auto edge = held & ~previous;
    auto now = millis();
    if (edge) {
      repeatAt = now + 400;
      previous = held;
      for (int i = 0; i < 12; ++i)
        if (edge & masks[i])
          return buttons[i];
    }
    const uint32_t dpad = masks[0] | masks[1] | masks[6] | masks[7];
    if (held == previous && (held & dpad) && int32_t(now - repeatAt) >= 0) {
      repeatAt = now + 110;
      return held & masks[0] ? Button::Up : held & masks[1] ? Button::Down : held & masks[6] ? Button::Left : Button::Right;
    }
    previous = held;
#endif
    return Button::None;
  }

  bool touch() {
#ifdef __vita__
    SceTouchData data{};
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &data, 1) < 0)
      return false;
    bool down = data.reportNum > 0;
    if (down) {
      int x = data.report[0].x / 2, y = data.report[0].y / 2;
      if (!touching) {
        touchX = x;
        touchY = dragY = y;
        dragged = false;
      } else if (std::abs(y - dragY) >= 24) {
        dragged = true;
        ui->scroll((dragY - y) / 24);
        dragY = y;
        return true;
      }
      touching = true;
      return false;
    }
    bool tapped = touching && !dragged;
    touching = false;
    if (tapped && armed)
      ui->touch(touchX, touchY);
    return tapped;
#else
    return false;
#endif
  }
};

void useLauncherLanguage() {
  int lang = -1;
  try {
    auto prefs = json5pp::parse(readFile(Paths().at("launcher-preferences.json"), 65536));
    auto &o = prefs.as_object();
    auto it = o.find("language");
    if (it != o.end() && it->second.is_string())
      lang = languageFromCode(it->second.as_string());
  } catch (...) {
  }
  setLanguage(lang >= 0 ? lang : systemLanguage());
}

GameMenu::GameMenu() : impl(new Impl) {}
GameMenu::~GameMenu() { close(); }

bool GameMenu::open(const std::string &configPath) {
  close();
  // config/<16 hex digits>.json
  auto slash = configPath.find_last_of('/');
  std::string name = slash == std::string::npos ? configPath : configPath.substr(slash + 1);
  if (name.size() != 21 || name.compare(16, 5, ".json") != 0)
    return false;
  std::string key = name.substr(0, 16);
  try {
    std::unique_ptr<UI> ui(new UI({}, false));
    Entry playing;
    if (!ui->model.activeGame(playing, key)) {
      std::fprintf(stderr, "Game menu: the running game was not found (%s)\n", key.c_str());
      return false;
    }
    useLauncherLanguage();
    ui->openInGame(playing);
    std::unique_ptr<View> view(new View("app0:/font.ttf"));
    view->overlay = true;
    view->gpu = true; // quads only; the engine draws them
    impl->ui = std::move(ui);
    impl->view = std::move(view);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Game menu: %s\n", e.what());
    close();
    return false;
  }
  impl->armed = false;
  impl->previous = 0;
  impl->touching = impl->dragged = false;
  impl->dirty = true;
  return true;
}

bool GameMenu::isOpen() const { return impl->ui != nullptr; }

void GameMenu::step() {
  if (!impl->ui)
    return;
  auto &ui = *impl->ui;
  try {
    bool acted = false;
    Button b = impl->button();
    if (b != Button::None) {
      ui.action(b);
      acted = true;
    }
    acted = impl->touch() || acted;
#ifdef __vita__
    // The controls tester shows what the buttons, sticks and screens do.
    if (ui.controlsTesting) {
      ui.controlsSample = vitaReadControls();
      ui.controlsResult = ui.controlsTestState.update(ui.controls, ui.controlsSample);
      acted = true;
    }
#endif
    // The clock and battery in the top bar.
    uint32_t now = millis();
    if (acted || now - impl->drawnAt > 15000) {
      impl->dirty = true;
#ifdef __vita__
      SceDateTime t{};
      if (sceRtcGetCurrentClockLocalTime(&t) >= 0) {
        char clock[8];
        std::snprintf(clock, sizeof clock, "%02d:%02d", t.hour, t.minute);
        ui.clockText = clock;
      }
      int battery = scePowerGetBatteryLifePercent();
      ui.batteryPercent = battery >= 0 && battery <= 100 ? battery : -1;
#endif
    }
  } catch (const std::exception &e) {
    ui.message = e.what();
    impl->dirty = true;
  }
}

GameMenu::Exit GameMenu::exit() const {
  if (!impl->ui)
    return Resume;
  return impl->ui->inGameExit == UI::BackToLibrary ? BackToLibrary
         : impl->ui->inGameExit == UI::Resume      ? Resume
                                                   : Staying;
}

bool GameMenu::takeChanged() {
  if (!impl->ui || !impl->ui->inGameChanged)
    return false;
  impl->ui->inGameChanged = false;
  return true;
}

std::string GameMenu::valuesJson() const { return impl->ui ? impl->ui->displayValues.stringify() : "{}"; }

const View *GameMenu::frame() {
  if (!impl->ui)
    return nullptr;
  if (impl->dirty) {
    impl->view->draw(*impl->ui);
    impl->dirty = false;
    impl->drawnAt = millis();
  }
  return impl->view.get();
}

void GameMenu::close() {
  if (!impl)
    return;
  impl->view.reset();
  impl->ui.reset();
}
} // namespace hardrpg
