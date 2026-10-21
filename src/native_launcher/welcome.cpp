// The welcome guide's state and input: a few slides shown once, on the first
// start, and again from Settings. Cross moves on, Circle goes back, Start
// skips to the end. Drawing is in welcome_view.cpp.
#include "home_layout.h"
#include "ui.h"
#include "welcome_layout.h"
#include <algorithm>

namespace hardrpg {
using namespace welcomeLayout;

void UI::openWelcome() {
  welcome.active = true;
  welcome.slide = 0;
  welcome.from = -1;
  welcome.amount = 1;
  welcome.languageFocus = language();
  message.clear();
}

void UI::finishWelcome() {
  welcome.active = false;
  welcome.from = -1;
  welcomeSeen = true;
  savePreferences();
  // A release found while the guide was up waits in Release checks.
}

void UI::welcomeAction(Button b) {
  auto go = [&](int step) {
    welcome.from = welcome.slide;
    welcome.slide += step;
    welcome.direction = step;
    welcome.amount = 0;
  };
  int last = Welcome::SlideCount - 1;
  if (b == Button::Cross || b == Button::Right) {
    if (welcome.slide == last)
      finishWelcome();
    else
      go(1);
  } else if (b == Button::Circle || b == Button::Left) {
    if (welcome.slide > 0)
      go(-1);
  } else if (b == Button::Start) {
    finishWelcome();
  } else if ((b == Button::Up || b == Button::Down) && welcome.slide == Welcome::Language) {
    welcome.languageFocus = (welcome.languageFocus + (b == Button::Up ? languageCount - 1 : 1)) % languageCount;
    selectLanguage(welcome.languageFocus);
  }
}

void UI::welcomeTap(int x, int y) {
  using namespace homeLayout;
  if (y >= hintsY) {
    for (auto &h : hintHit)
      if (inside(x, y, h.x, hintsY, h.w, 544 - hintsY)) {
        welcomeAction(h.button);
        return;
      }
    return;
  }
  if (welcome.slide == Welcome::Language)
    for (int i = 0; i < languageCount; ++i)
      if (inside(x, y, slideX + textX, slideY + langTop + i * langRowH, textW, langBox)) {
        welcome.languageFocus = i;
        selectLanguage(i);
        return;
      }
}

} // namespace hardrpg
