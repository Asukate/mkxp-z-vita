// Drawing for the welcome guide (see welcome.cpp for its state and input).
// Each slide's text is one cached layer. What moves is quads: changing slides
// slides and fades the layers, the example folder lists drop in row by row,
// and the library view previews play the carousel and the grid.
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
double ease(double t) {
  t = std::max(0.0, std::min(1.0, t));
  return 1 - std::pow(1 - t, 3);
}
// An example folder list: what a slide shows inside the games or RTP folder.
struct TreeRow {
  bool zip;
  const char *name, *note; // note: English, shown translated, or null
};
const TreeRow gameRows[] = {{false, "Ib", nullptr},
                            {false, "OFF", nullptr},
                            {false, "Yume Nikki", nullptr},
                            {false, "The Witch's House", nullptr}};
const TreeRow rtpRows[] = {{false, "Standard", "RPG Maker XP"},
                           {false, "RPGVX", "RPG Maker VX"},
                           {true, "RPGVXAce.zip", "RPG Maker VX Ace"},
                           {false, "2000", "RPG Maker 2000"},
                           {false, "2003", "RPG Maker 2003"}};
int treeRows(int slide) {
  return slide == Welcome::Games ? 4 : slide == Welcome::Rtp ? 5 : 0;
}
const TreeRow &treeRow(int slide, int i) { return slide == Welcome::Games ? gameRows[i] : rtpRows[i]; }
int treeHeight(int rows) { return treeHeaderH + rows * treeRowH + 6; }
// Cover colours for the view previews, dark like generated covers.
const Color coverColors[] = {{74, 38, 50}, {38, 52, 74}, {50, 66, 46}, {78, 62, 40}, {58, 44, 78}, {36, 64, 68}, {70, 70, 76}};
} // namespace

// One slide's text as a layer: a small heading, the title, then its text.
// Where the slide's moving part sits (the folder list, the view previews) is
// kept in welcomeExtra.
const Image &View::welcomeSlide(const UI &u, int slide) {
  std::string key = std::to_string(slide) + "|" + languageCode(language()) + "|" + std::to_string(u.welcome.languageFocus) + "|" +
                    std::to_string(u.home.grid) + "|" + u.model.paths.root;
  auto &layer = welcomeCache[key];
  if (!layer.key.empty())
    return layer.image;
  beginLayer(layer, key, slideW, slideH);
  int y = 0;
  auto heading = [&](const char *kicker, const char *title) {
    label(textX, y, tr(kicker), 13, ruby);
    label(textX, y + 17, tr(title), 34, white, 255, true, -1, textW);
    y += 64;
  };
  auto paragraph = [&](const std::string &text, int size = 18, Color color = soft, int after = 10) {
    for (auto &line : wrap(text, size, textW)) {
      label(textX, y, line, size, color);
      y += size + 7;
    }
    y += after;
  };
  // Something easy to miss: a ruby bar beside the text.
  auto reminder = [&](const std::string &text) {
    auto lines = wrap(text, 15, textW - 30);
    int h = int(lines.size()) * 21 + 14;
    rect(textX, y, textW, h, white, 14, 8);
    rect(textX, y, 4, h, ruby, 255, 2);
    for (size_t i = 0; i < lines.size(); ++i)
      label(textX + 18, y + 7 + int(i) * 21, lines[i], 15, soft);
    y += h + 10;
  };
  // The folder list: its header (the folder) is text here; the box and the
  // rows are drawn by buildWelcome so they can drop in.
  auto tree = [&](const std::string &folder, int rows) {
    welcomeExtra[key] = y;
    blitScaled(icon("folder-26"), textX + 14, y + 7, 22, 22);
    label(textX + 48, y + 7, folder, 18, white, 255, false, -1, textW - 64);
    y += treeHeight(rows) + 10;
  };
  switch (slide) {
  case Welcome::Language:
    heading("Welcome", "Welcome to HardRPG");
    paragraph(tr("Choose your language. You can change it later in Settings."));
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
    paragraph(tr("Copy each game into this folder, as a folder:"), 18, soft, 6);
    tree(u.model.paths.at("games") + "/", treeRows(slide));
    reminder(tr("A game that came as a ZIP has to be unzipped first, on your PC or on the Vita with VitaShell. Folders start "
                "right away."));
    paragraph(tr("Games kept elsewhere can stay there: add their folder in Settings, Game folders, or press <start> on the home "
                 "screen."),
              15, muted);
    break;
  case Welcome::Rtp:
    heading("The RTP", "Many games need the RTP");
    paragraph(tr("The RTP is RPG Maker's shared graphics and music. A game that does not include it will not start without it."),
              18, soft, 6);
    tree(u.model.paths.at("rtp") + "/", treeRows(slide));
    reminder(tr("Use these names: HardRPG looks for them. XP, VX and VXAce work too, and so does a folder inside them. An RTP kept "
                "anywhere else can be chosen in Settings, RTP locations."));
    break;
  case Welcome::View: {
    heading("Library", "How your games are shown");
    paragraph(tr("Choose the view the library opens in."));
    // Under each preview: its icon and name, and a ruby dot on the chosen one.
    const char *names[] = {"Carousel", "Grid"}, *icons[] = {"view-carousel-31", "view-grid-31"};
    for (int i = 0; i < 2; ++i) {
      int x = viewX(i), ly = viewTop + viewH + 12;
      bool chosen = u.home.grid == (i == 1);
      blitScaled(icon(icons[i]), x + 4, ly, 24, 24, chosen ? 255 : 150);
      label(x + 38, ly, tr(names[i]), 20, chosen ? white : muted, 255, true);
      if (chosen)
        rect(x + viewW - 18, ly + 7, 12, 12, ruby, 255, 6);
    }
    y = viewTop + viewH + viewLabelH + 12;
    paragraph(tr("You can switch any time with the first button on the library's toolbar; the library keeps the view you used last."),
              15, muted);
    break;
  }
  case Welcome::Defaults: {
    heading("Good to know", "A few defaults");
    struct Item {
      const char *icon, *name, *text;
    } items[] = {
        {"view-grid-31", "Screen", "Games keep their shape and fill the screen as far as they can, with the Sharp filter. Change it in "
                                   "Settings, Game display."},
        {"btn-triangle-26", "Each game", "Press <triangle> Triangle on a game for favourites, game info and its own settings and controls."},
        {"sort-time-24", "Fast forward", "Press <l>+<r> while playing to run up to twice as fast."},
        {"view-carousel-31", "Back to HardRPG", "Hold <l>+<r>+<select> in a game to come back here. Progress you have not saved is lost."},
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
    paragraph(tr("HardRPG downloads covers, backgrounds, logos and details such as the year and developer by itself while it is "
                 "open and online, one game at a time. Press <triangle> Triangle on a game and choose Change artwork and info to look "
                 "again and pick the pictures."));
    paragraph(tr("Some sources work right away. Others need a free API key, which you can add in Settings, Game info and artwork. "
                 "Each source there says where to get its key."));
    paragraph(tr("Your own pictures and music: put cover, hero (the background), logo (PNG or JPG) or theme.ogg in "
                 "ux0:/data/hardrpg/art/<game name>/. They are used instead of downloaded ones."), 16, muted);
    break;
  }
  endLayer();
  // Keep a few slides; the guide is short.
  if (welcomeCache.size() > 10)
    for (auto it = welcomeCache.begin(); it != welcomeCache.end();)
      it = it->first == key ? std::next(it) : welcomeCache.erase(it);
  return welcomeCache[key].image;
}

// A slide's moving parts, at horizontal offset ox and opacity alpha. clock is
// how long the slide has been in; a slide going out shows everything.
void View::welcomeMotion(const UI &u, int slide, double ox, double alpha, double clock, bool behind) {
  auto quad = [&](const Image *img, double x, double y, double w, double h, double a, double radius = 0) {
    Quad q;
    q.image = img;
    q.x = float(x), q.y = float(y), q.w = float(w), q.h = float(h), q.alpha = float(a * alpha), q.radius = float(radius);
    scene.push_back(q);
    return &scene.back();
  };
  auto solid = [&](Color c, double x, double y, double w, double h, double a, double radius = 0) {
    quad(nullptr, x, y, w, h, a, radius)->color = c;
  };
  double left = slideX + ox;
  if (int rows = treeRows(slide)) {
    std::string key = std::to_string(slide) + "|" + languageCode(language()) + "|" + std::to_string(u.welcome.languageFocus) + "|" +
                      std::to_string(u.home.grid) + "|" + u.model.paths.root;
    double top = slideY + welcomeExtra[key];
    // Row i drops in after the one before it.
    auto shown = [&](int i) { return ease((clock - 0.25 - i * 0.12) / 0.3); };
    double open = 0;
    for (int i = 0; i < rows; ++i)
      open += shown(i);
    if (behind) {
      // The box grows as the rows come in; a thin line joins them to the folder.
      solid(white, left + textX, top, textW, treeHeaderH + open * treeRowH + (open > 0 ? 6 : 0), 0.086, 10);
      if (open > 0)
        solid(white, left + textX + 26, top + treeHeaderH - 6, 1, (open - 0.5) * treeRowH + 6, 0.25);
      return;
    }
    for (int i = 0; i < rows; ++i) {
      double p = shown(i);
      if (p <= 0)
        continue;
      auto &r = treeRow(slide, i);
      std::string note = r.note ? tr(r.note) : "";
      std::string rowKey = std::string("tree|") + r.name + "|" + note;
      auto &layer = rowCache[rowKey];
      int w = textW - treeIndent - 8;
      if (layer.key.empty()) {
        beginLayer(layer, rowKey, w, treeRowH);
        rect(0, treeRowH / 2, 12, 1, white, 64);
        blitScaled(icon(r.zip ? "memcard-26" : "folder-26"), 18, (treeRowH - 20) / 2, 20, 20);
        label(46, (treeRowH - 19) / 2, r.name, 17, white);
        if (!note.empty())
          label(w - 14, (treeRowH - 17) / 2, note, 14, r.zip && slide == Welcome::Games ? Color{255, 120, 138} : muted, 255, false, 1);
        endLayer();
      }
      quad(&layer.image, left + textX + 26, top + treeHeaderH + i * treeRowH - (1 - p) * 12, w, treeRowH, p);
    }
    return;
  }
  if (slide != Welcome::View)
    return;
  // The library view previews: a carousel that moves on a card at a time and
  // a grid whose selection walks across it, as the library does.
  for (int i = 0; i < 2; ++i) {
    double px = left + viewX(i), py = slideY + viewTop;
    bool chosen = u.home.grid == (i == 1);
    if (behind) {
      if (chosen)
        solid(ruby, px - 3, py - 3, viewW + 6, viewH + 6, 1, 13);
      solid({16, 16, 18}, px, py, viewW, viewH, 1, 10);
      continue;
    }
    if (i == 0) {
      // Carousel: every 1.6 s the next card slides to the middle and grows.
      const double period = 1.6;
      int step = int(clock / period);
      double pos = step + ease((clock - step * period) / 0.5);
      double cx = px + viewW / 2.0, cy = py + viewH / 2.0 - 10;
      std::vector<std::pair<double, int>> order;
      for (int j = int(pos) - 3; j <= int(pos) + 3; ++j)
        order.push_back({std::fabs(j - pos), j});
      std::sort(order.rbegin(), order.rend());
      for (auto &o : order) {
        double d = o.second - pos, ad = std::fabs(d), near = std::max(0.0, 1 - ad);
        double a = std::max(0.0, std::min(1.0, (2.5 - ad) / 0.6));
        if (a <= 0)
          continue;
        double s = 1 + 0.4 * near, w = 46 * s, h = 69 * s;
        double x = cx + d * 58 + (d > 0 ? 1 : d < 0 ? -1 : 0) * std::min(ad, 1.0) * 14 - w / 2;
        double y = cy - h / 2 + (1 - near) * 4;
        auto &c = coverColors[((o.second % 7) + 7) % 7];
        solid(c, x, y, w, h, a, 5);
        solid(ruby, x + 6 * s, y + h - 12 * s, 12 * s, 2, a * 0.9);
      }
      solid(ruby, cx - 10, cy + 54, 20, 2, 1, 1);
      solid(white, cx - 34, cy + 64, 68, 6, 0.75, 3);
      solid(white, cx - 22, cy + 75, 44, 4, 0.3, 2);
    } else {
      // Grid: five columns, the selection moving every 0.8 s.
      const int cols = 5, tw = 40, th = 58, gx = 12, gy = 16;
      double ox0 = px + (viewW - (cols * tw + (cols - 1) * gx)) / 2.0, oy0 = py + (viewH - (2 * th + gy)) / 2.0 - 4;
      int sel = int(clock / 0.8) % (cols * 2);
      for (int k = 0; k < cols * 2; ++k) {
        bool here = k == sel;
        double s = here ? 1.08 : 1, w = tw * s, h = th * s;
        double x = ox0 + (k % cols) * (tw + gx) - (w - tw) / 2, y = oy0 + (k / cols) * (th + gy) - (h - th) / 2;
        solid(coverColors[k % 7], x, y, w, h, here ? 1 : 0.8, 4);
        if (here)
          solid(ruby, ox0 + (k % cols) * (tw + gx), oy0 + (k / cols) * (th + gy) + th + 4, 14, 2, 1);
      }
    }
  }
}

void View::buildWelcome(const UI &u) {
  using namespace homeLayout;
  scene.clear();
  ensureStrips();
  pendingLoads = coversToMake = false;
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

  // The slide coming in, and the one going out, eased; each slide's moving
  // parts go behind and over its text.
  double t = std::max(0.0, std::min(1.0, u.welcome.amount));
  double e = ease(t);
  const int shift = 70;
  auto slide = [&](int which, double ox, double alpha, double clock) {
    const Image &text = welcomeSlide(u, which);
    welcomeMotion(u, which, ox, alpha, clock, true);
    quad(&text, slideX + ox, slideY, slideW, slideH, alpha);
    welcomeMotion(u, which, ox, alpha, clock, false);
  };
  if (u.welcome.from >= 0 && t < 1)
    slide(u.welcome.from, -u.welcome.direction * e * shift, 1 - e, 100);
  slide(u.welcome.slide, u.welcome.direction * (1 - e) * shift, e, u.welcome.clock);

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
  if (u.welcome.slide == Welcome::View)
    right.push_back({"\xe2\x86\x90 \xe2\x86\x92", tr("Choose"), Button::None});
  if (!last)
    right.push_back({"START", tr("Skip"), Button::Start});
  hintBar(u, left, right, "");
}
} // namespace hardrpg
