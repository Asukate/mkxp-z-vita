// Drawing for the welcome guide (see welcome.cpp for its state and input).
// Each slide is one cached text layer; changing slides slides and fades the
// two layers, so the GPU animates it without redrawing text.
#include "home_layout.h"
#include "i18n.h"
#include "view.h"
#include "welcome_layout.h"
#include <algorithm>
#include <cmath>

namespace hardrpg {
using namespace welcomeLayout;
namespace {
using Color = std::array<int, 3>;
const Color white{255, 255, 255}, ink{7, 7, 7}, muted{169, 169, 173}, ruby{224, 35, 63}, soft{214, 214, 216};
} // namespace

// One slide as a layer: a small heading, the title, then its text.
const Image &View::welcomeSlide(const UI &u, int slide) {
  std::string key = std::to_string(slide) + "|" + languageCode(language()) + "|" + std::to_string(u.welcome.languageFocus) + "|" +
                    u.model.paths.root;
  auto &layer = welcomeCache[key];
  if (!layer.key.empty())
    return layer.image;
  beginLayer(layer, key, slideW, slideH);
  int y = 0;
  auto heading = [&](const char *kicker, const char *title) {
    label(textX, y, tr(kicker), 13, ruby);
    label(textX, y + 18, tr(title), 36, white, 255, true, -1, textW);
    y += 70;
  };
  auto paragraph = [&](const std::string &text, int size = 18, Color color = soft) {
    for (auto &line : wrap(text, size, textW)) {
      label(textX, y, line, size, color);
      y += size + 7;
    }
    y += 10;
  };
  // A path on the memory card, in a box with a folder icon.
  auto path = [&](const std::string &where) {
    rect(textX, y, textW, 46, white, 22, 10);
    blitScaled(icon("folder-26"), textX + 14, y + 11, 24, 24);
    label(textX + 50, y + 12, where, 19, white, 255, false, -1, textW - 64);
    y += 62;
  };
  switch (slide) {
  case Welcome::Language:
    heading("Welcome", "Welcome to HardRPG");
    paragraph(tr("Choose your language. You can change it later in Settings."));
    y = langTop;
    for (int i = 0; i < languageCount; ++i) {
      int ry = langTop + i * langRowH;
      bool focused = u.welcome.languageFocus == i, chosen = language() == i;
      rect(textX, ry, textW, langBox, white, focused ? 41 : 15, 8);
      if (focused)
        rect(textX, ry, 4, langBox, ruby, 255, 2);
      label(textX + 20, ry + 12, languageName(i), 20, white);
      if (chosen)
        rect(textX + textW - 32, ry + langBox / 2 - 6, 12, 12, ruby, 255, 6);
    }
    break;
  case Welcome::Games:
    heading("Your games", "Where your games go");
    paragraph(tr("Copy each game into this folder, as a folder or a ZIP file:"));
    path(u.model.paths.at("games") + "/");
    paragraph(tr("HardRPG looks for new games there each time it starts."));
    paragraph(tr("Games somewhere else can stay where they are: add their folder in Settings, Game folders, or press Start on the "
                 "home screen."),
              16, muted);
    break;
  case Welcome::Rtp:
    heading("The RTP", "Many games need the RTP");
    paragraph(tr("The RTP is RPG Maker's shared graphics and music. A game that does not include it will not start without it."));
    path(u.model.paths.at("rtp") + "/");
    paragraph(tr("Put each RTP there in its own folder or ZIP, named XP, VX, VXAce, 2000 or 2003."));
    paragraph(tr("Settings, RTP locations shows which ones were found, and can use an RTP kept anywhere else."), 16, muted);
    break;
  case Welcome::Defaults: {
    heading("Good to know", "A few defaults");
    struct Item {
      const char *icon, *name, *text;
    } items[] = {
        {"view-grid-31", "Screen", "Games keep their shape and fill the screen as far as they can, with the Sharp filter. Change it in "
                                   "Settings, Game display."},
        {"btn-triangle-26", "Each game", "Press Triangle on a game for favourites, game info and its own settings and controls."},
        {"sort-time-24", "Fast forward", "Press L+R while playing to run up to twice as fast."},
        {"view-carousel-31", "Back to HardRPG", "Hold L+R+Select in a game to come back here. Progress you have not saved is lost."},
    };
    for (auto &item : items) {
      const Image &img = icon(item.icon);
      blitScaled(img, textX, y + 2, 24, 24);
      label(textX + 40, y, tr(item.name), 18, white, 255, true);
      y += 24;
      for (auto &line : wrap(tr(item.text), 16, textW - 40)) {
        label(textX + 40, y, line, 16, muted);
        y += 22;
      }
      y += 12;
    }
    break;
  }
  default:
    heading("Game info", "Covers and game details");
    paragraph(tr("HardRPG can download covers, backgrounds, logos and details such as the year and developer. Press Triangle on a "
                 "game and choose Download game info."));
    paragraph(tr("Some sources work right away. Others need a free API key, which you can add in Settings, Game info and artwork. "
                 "Each source there says where to get its key."));
    paragraph(tr("You can always keep the pictures you have: the artwork chooser shows what each source found first."), 16, muted);
    break;
  }
  endLayer();
  // Keep a few slides; the guide is short.
  if (welcomeCache.size() > 8)
    for (auto it = welcomeCache.begin(); it != welcomeCache.end();)
      it = it->first == key ? std::next(it) : welcomeCache.erase(it);
  return welcomeCache[key].image;
}

void View::buildWelcome(const UI &u) {
  using namespace homeLayout;
  scene.clear();
  ensureStrips();
  pendingLoads = false;
  auto quad = [&](const Image *img, double x, double y, double w, double h, double alpha = 1, double radius = 0) {
    Quad q;
    q.image = img;
    q.x = float(x), q.y = float(y), q.w = float(w), q.h = float(h), q.alpha = float(alpha), q.radius = float(radius);
    scene.push_back(q);
    return &scene.back();
  };
  auto solid = [&](Color c, double x, double y, double w, double h, double alpha, double radius = 0) {
    quad(nullptr, x, y, w, h, alpha, radius)->color = c;
  };
  solid(ink, 0, 0, width, height, 1);
  quad(&glow, 0, 0, width, height * 0.75);
  quad(&tint, 0, 0, width, height);
  const Image &emblem = icon("emblem-40"), &wordmark = icon("wordmark-25");
  quad(&emblem, 16, 11, emblem.width, emblem.height);
  quad(&wordmark, 64, 19, wordmark.width, wordmark.height);

  // The slide coming in, and the one going out, eased.
  double t = std::max(0.0, std::min(1.0, u.welcome.amount));
  double e = 1 - std::pow(1 - t, 3);
  const int shift = 70;
  if (u.welcome.from >= 0 && t < 1) {
    const Image &old = welcomeSlide(u, u.welcome.from);
    quad(&old, slideX - u.welcome.direction * e * shift, slideY, slideW, slideH, 1 - e);
  }
  const Image &now = welcomeSlide(u, u.welcome.slide);
  quad(&now, slideX + u.welcome.direction * (1 - e) * shift, slideY, slideW, slideH, e);

  // Where in the guide: a dot per slide, the current one a ruby bar.
  int count = Welcome::SlideCount, gap = 18, x = 480 - ((count - 1) * gap + 22) / 2;
  for (int i = 0; i < count; ++i) {
    bool here = i == u.welcome.slide;
    solid(here ? ruby : white, x, dotsY, here ? 22 : 8, 8, here ? 1 : 0.3, 4);
    x += here ? 22 + gap - 8 : gap;
  }

  quad(&band, 0, hintsY - 4, width, height - (hintsY - 4));
  bool last = u.welcome.slide == Welcome::SlideCount - 1;
  std::vector<Hint> left = {{"btn-cross-26", tr(last ? "Start playing" : "Next"), Button::Cross}};
  if (u.welcome.slide > 0)
    left.push_back({"btn-circle-26", tr("Back"), Button::Circle});
  std::vector<Hint> right;
  if (u.welcome.slide == Welcome::Language)
    right.push_back({"UP DOWN", tr("Language"), Button::None});
  if (!last)
    right.push_back({"START", tr("Skip"), Button::Start});
  hintBar(u, left, right, "");
}
} // namespace hardrpg
