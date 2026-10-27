// Drawing for the home screen (see home.cpp for its state and input).
//
// The screen is a list of quads: background art, scrims, covers and small
// text layers. Moving things (covers, fades, slides) are only quad positions
// and alphas, so the Vita's GPU draws them every frame for free. Text and
// icons are drawn on the CPU into layers, and only when their content changes.
#include "home_layout.h"
#include "i18n.h"
#include "release.h"
#include "util/json5pp.hpp"
#include "view.h"
#include <algorithm>
#include <functional>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace hardrpg {
using namespace homeLayout;
namespace {
constexpr const char *homeVersion = HARDRPG_VERSION;
using Color = std::array<int, 3>;
const Color white{255, 255, 255}, ink{7, 7, 7}, muted{169, 169, 173}, faint{111, 111, 117},
    ruby{224, 35, 63}, rubyHi{255, 77, 99};
// First letters of up to two significant words, for generated covers.
std::string initials(const std::string &name) {
  std::string out, word;
  auto flush = [&] {
    std::string l = lower(word);
    if (!word.empty() && l != "the" && l != "of" && l != "a" && out.size() < 2)
      out += char(std::toupper(static_cast<unsigned char>(word[0])));
    word.clear();
  };
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)))
      word += c;
    else
      flush();
  }
  flush();
  return out.empty() ? "?" : out;
}
std::string playtime(int64_t seconds, bool suffix = true) {
  if (seconds <= 0)
    return suffix ? tr("Not played yet") : "-";
  int64_t minutes = seconds / 60;
  char buf[40];
  if (minutes < 60)
    std::snprintf(buf, sizeof buf, "%lld min", (long long)std::max<int64_t>(1, minutes));
  else
    std::snprintf(buf, sizeof buf, "%lld h %02lld min", (long long)(minutes / 60), (long long)(minutes % 60));
  return suffix ? trf("{} played", {buf}) : buf;
}
std::string lastPlayed(int64_t when) {
  if (!when)
    return tr("Never");
  int64_t days = (int64_t(std::time(nullptr)) - when) / 86400;
  if (days <= 0)
    return tr("Today");
  if (days == 1)
    return tr("Yesterday");
  if (days < 7)
    return trf("{} days ago", {std::to_string(days)});
  if (days < 14)
    return tr("Last week");
  if (days < 60)
    return trf("{} weeks ago", {std::to_string(days / 7)});
  return trf("{} months ago", {std::to_string(days / 30)});
}
// Short names for the engine tabs, so more engines fit in the toolbar.
const char *engineTab(int version) {
  static const char *names[] = {"All", "XP", "VX", "Ace", "2k/2k3", "MV", "MZ"};
  return version >= 0 && version <= formatMZ ? tr(names[version]) : "?";
}
const char *engineName(int version) {
  static const char *names[] = {"Collection", "XP", "VX", "VX Ace", "2000/2003", "MV", "MZ"};
  return tr(version >= 0 && version <= formatMZ ? names[version] : "Game");
}
inline void over(uint8_t *d, int r, int g, int b, int a) {
  // Premultiplied source over a premultiplied destination (opaque or not).
  d[0] = r + (d[0] * (255 - a) + 127) / 255;
  d[1] = g + (d[1] * (255 - a) + 127) / 255;
  d[2] = b + (d[2] * (255 - a) + 127) / 255;
  d[3] = a + (d[3] * (255 - a) + 127) / 255;
}
// A one-pixel-wide (or tall) gradient image, stretched by the quad.
Image strip(int w, int h, const std::function<std::array<int, 4>(double)> &at) {
  Image img = Image::blank(w, h);
  int n = std::max(w, h);
  for (int i = 0; i < n; ++i) {
    auto c = at(n > 1 ? double(i) / (n - 1) : 0);
    uint8_t *p = &img.rgba[size_t(i) * 4];
    p[3] = c[3];
    for (int k = 0; k < 3; ++k)
      p[k] = c[k] * c[3] / 255;
  }
  return img;
}
} // namespace

// Whether the next frame would look different. A busy loader alone is not
// a reason: the screen is drawn again when a picture is ready, so it does
// not redraw every frame while covers and backgrounds load.
bool View::animating() const {
  return marqueeMoving || coversToMake || (loader && loader->ready()) ||
         (fading && std::chrono::steady_clock::now() - fadeStart < std::chrono::milliseconds(320));
}

const Image &View::icon(const std::string &name) {
  auto found = icons.find(name);
  if (found != icons.end())
    return found->second;
  // "name:ruby" is the white icon recoloured ruby.
  auto colon = name.find(':');
  Image img = Image::load(assets + "ui/" + name.substr(0, colon) + ".png");
  if (colon != std::string::npos)
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
      img.rgba[i + 1] = img.rgba[i + 1] * 77 / 255;
      img.rgba[i + 2] = img.rgba[i + 2] * 99 / 255;
    }
  return icons.emplace(name, std::move(img)).first->second;
}

View::Art &View::artFor(const UI &u, const Entry &e, double distance, bool needHero) {
  auto id = u.entryKey(e);
  Art &a = art[id];
  a.used = frameNumber;
  int fw = int(cardW * focusScale), fh = int(cardH * focusScale);
  if (a.generate) {
    // A generated cover: dark card, large initials, a ruby rule and the name.
    if (loadBudget <= 0) {
      pendingLoads = coversToMake = true;
      return a;
    }
    --loadBudget;
    a.generate = false;
    a.cover = Image::blank(fw, fh);
    uint8_t *saved = surface;
    int sw = surfaceW, sh = surfaceH;
    surface = a.cover.rgba.data();
    surfaceW = fw;
    surfaceH = fh;
    rect(0, 0, fw, fh, {28, 28, 31}, 255);
    label(18, 16, initials(e.name), 76, {236, 236, 238}, 255, false);
    rect(18, fh - 74, 40, 4, ruby, 255);
    label(18, fh - 60, e.name, 19, {190, 190, 194}, 255, false, -1, fw - 32);
    surface = saved;
    surfaceW = sw;
    surfaceH = sh;
    a.cover.touch();
    loader->store(a.generatedPath, a.cover);
  }
  bool wantCover = !a.coverLoaded, wantHero = needHero && !a.heroLoaded;
  if (!wantCover && !wantHero)
    return a;
  ArtLoader::Request r;
  r.key = id;
  r.root = u.model.paths.at("art/");
  r.names = u.artFolders(e);
  r.cache = u.model.paths.at("cache/art/");
  r.title = e.name;
  r.blurAdult = u.blurAdult;
  if (wantCover) {
    r.width = fw;
    r.height = fh;
    loader->want(r, distance);
  }
  if (wantHero) {
    r.hero = true;
    r.width = width;
    r.height = height;
    loader->want(r, -1);
  }
  return a;
}

// Takes what the loader finished since the last frame.
void View::receiveArt(const UI &u) {
  ArtLoader::Result r;
  while (loader->take(r)) {
    auto found = art.find(r.key);
    if (found == art.end() || r.cancelled)
      continue;
    Art &a = found->second;
    if (r.shots && r.shot) {
      if (!r.pictures.empty()) {
        a.viewed = std::move(r.pictures[0]);
        a.viewedShot = r.shot;
      }
      continue;
    }
    if (r.shots) {
      a.shotsLoaded = true;
      a.shots = std::move(r.pictures);
      continue;
    }
    if (!r.hero) {
      a.coverLoaded = true;
      a.generate = r.cover.empty();
      a.generatedPath = r.generatedPath;
      a.info = std::move(r.info);
      if (r.theme.empty())
        u.customThemes.erase(r.key);
      else
        u.customThemes[r.key] = r.theme;
      a.cover = std::move(r.cover);
      a.logo = std::move(r.logo);
      continue;
    }
    a.heroLoaded = true;
    a.hero = std::move(r.background);
    if (a.hero.empty())
      continue;
    // Backgrounds are 2 MB each: keep only the last few.
    heroOrder.erase(std::remove(heroOrder.begin(), heroOrder.end(), r.key), heroOrder.end());
    heroOrder.push_back(r.key);
    for (size_t k = 0; heroOrder.size() > 3 && k < heroOrder.size();) {
      auto old = art.find(heroOrder[k]);
      if (old != art.end() && (&old->second.hero == heroShown || &old->second.hero == heroFading)) {
        ++k;
        continue;
      }
      if (old != art.end()) {
        old->second.hero = Image{};
        old->second.heroLoaded = false;
      }
      heroOrder.erase(heroOrder.begin() + k);
    }
  }
}

// Forgets the games drawn longest ago once there are many; never one on screen.
void View::trimArt() {
  if (art.size() <= 64)
    return;
  std::vector<std::pair<uint64_t, std::string>> old;
  for (auto &kv : art)
    if (kv.second.used != frameNumber && &kv.second.hero != heroShown && &kv.second.hero != heroFading)
      old.push_back({kv.second.used, kv.first});
  std::sort(old.begin(), old.end());
  for (size_t k = 0; k < old.size() && art.size() > 48; ++k) {
    heroOrder.erase(std::remove(heroOrder.begin(), heroOrder.end(), old[k].second), heroOrder.end());
    art.erase(old[k].second);
  }
}

void View::rect(int x, int y, int w, int h, Color c, int alpha, int radius) {
  int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(surfaceW, x + w), y1 = std::min(surfaceH, y + h);
  for (int row = y0; row < y1; ++row) {
    uint8_t *d = &surface[(size_t(row) * surfaceW + x0) * 4];
    for (int col = x0; col < x1; ++col, d += 4) {
      int a = alpha;
      if (radius) {
        // Anti-aliased rounded corners.
        int cx = col < x + radius ? x + radius : col >= x + w - radius ? x + w - radius - 1 : col;
        int cy = row < y + radius ? y + radius : row >= y + h - radius ? y + h - radius - 1 : row;
        if (cx != col && cy != row) {
          double cover = std::max(0.0, std::min(1.0, radius + 0.5 - std::hypot(col - cx, row - cy)));
          a = int(a * cover);
        }
      }
      if (a >= 255)
        d[0] = c[0], d[1] = c[1], d[2] = c[2], d[3] = 255;
      else if (a > 0)
        over(d, c[0] * a / 255, c[1] * a / 255, c[2] * a / 255, a);
    }
  }
}

void View::blit(const Image &img, int x, int y, int alpha) {
  if (img.empty())
    return;
  for (int row = std::max(0, -y); row < img.height && y + row < surfaceH; ++row) {
    int start = std::max(0, -x), end = std::min(img.width, surfaceW - x);
    const uint8_t *s = &img.rgba[(size_t(row) * img.width + start) * 4];
    uint8_t *d = &surface[(size_t(y + row) * surfaceW + x + start) * 4];
    for (int col = start; col < end; ++col, s += 4, d += 4) {
      int a = s[3] * alpha / 255;
      if (a)
        over(d, s[0] * alpha / 255, s[1] * alpha / 255, s[2] * alpha / 255, a);
    }
  }
}

void View::blitScaled(const Image &img, double fx, double fy, double fw, double fh, int alpha, int brightness, int radius) {
  if (img.empty() || fw < 1 || fh < 1)
    return;
  int x = int(std::lround(fx)), y = int(std::lround(fy)), w = int(std::lround(fw)), h = int(std::lround(fh));
  int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(surfaceW, x + w), y1 = std::min(surfaceH, y + h);
  if (x0 >= x1 || y0 >= y1)
    return;
  int64_t stepX = (int64_t(img.width) << 16) / w, stepY = (int64_t(img.height) << 16) / h;
  int scale = alpha * brightness / 256; // colour multiplier out of 255
  for (int row = y0; row < y1; ++row) {
    int sy = std::min(img.height - 1, int(((row - y) * stepY + stepY / 2) >> 16));
    const uint8_t *src = &img.rgba[size_t(sy) * img.width * 4];
    uint8_t *d = &surface[(size_t(row) * surfaceW + x0) * 4];
    bool cornerRow = radius && (row < y + radius || row >= y + h - radius);
    int64_t sx = (x0 - x) * stepX + stepX / 2;
    for (int col = x0; col < x1; ++col, d += 4, sx += stepX) {
      const uint8_t *s = src + std::min(img.width - 1, int(sx >> 16)) * 4;
      int a = s[3] * alpha / 255, k = scale;
      if (cornerRow && (col < x + radius || col >= x + w - radius)) {
        int cx = col < x + radius ? x + radius : x + w - radius - 1;
        int cy = row < y + radius ? y + radius : y + h - radius - 1;
        double cover = std::max(0.0, std::min(1.0, radius + 0.5 - std::hypot(col - cx, row - cy)));
        a = int(a * cover);
        k = int(k * cover);
      }
      if (a)
        over(d, std::min(255, s[0] * k / 255), std::min(255, s[1] * k / 255), std::min(255, s[2] * k / 255), a);
    }
  }
}

namespace {
uint32_t decode(const std::string &s, size_t &i) {
  unsigned char b = s[i];
  if (b < 128) {
    ++i;
    return b;
  }
  int n = b >= 0xf0 ? 3 : b >= 0xe0 ? 2 : 1;
  uint32_t c = b & (0x3f >> n);
  ++i;
  for (int k = 0; k < n && i < s.size(); ++k, ++i)
    c = (c << 6) | (static_cast<unsigned char>(s[i]) & 63);
  return c;
}
} // namespace

// Button tokens in text: "<cross>" draws the Cross icon, "<l>" an L badge.
namespace {
struct ButtonToken {
  const char *name, *icon, *badge;
};
const ButtonToken buttonTokens[] = {
    {"cross", "btn-cross-26", nullptr},   {"circle", "btn-circle-26", nullptr}, {"square", "btn-square-26", nullptr},
    {"triangle", "btn-triangle-26", nullptr}, {"start", nullptr, "START"},     {"select", nullptr, "SELECT"},
    {"l", nullptr, "L"},                  {"r", nullptr, "R"},                  {"up", nullptr, "\xe2\x86\x91"},
    {"down", nullptr, "\xe2\x86\x93"},   {"left", nullptr, "\xe2\x86\x90"},   {"right", nullptr, "\xe2\x86\x92"},
};
// The token starting at s[i], or null; len is its length with the brackets.
const ButtonToken *buttonToken(const std::string &s, size_t i, size_t &len) {
  if (s[i] != '<')
    return nullptr;
  auto end = s.find('>', i);
  if (end == std::string::npos || end - i > 10)
    return nullptr;
  for (auto &t : buttonTokens)
    if (s.compare(i + 1, end - i - 1, t.name) == 0 && std::strlen(t.name) == end - i - 1) {
      len = end - i + 1;
      return &t;
    }
  return nullptr;
}
} // namespace

int View::tokenWidth(const void *token, int size) {
  auto &t = *static_cast<const ButtonToken *>(token);
  if (t.icon)
    return size + 6;
  return measure(t.badge, std::max(9, size * 2 / 3)) + 12;
}

void View::drawToken(const void *token, int x, int y, int size, int alpha) {
  auto &t = *static_cast<const ButtonToken *>(token);
  int line = size + size / 5; // about the height of a line of text
  if (t.icon) {
    blitScaled(icon(t.icon), x + 1, y + (line - size - 2) / 2, size + 2, size + 2, alpha);
    return;
  }
  int small = std::max(9, size * 2 / 3), w = tokenWidth(token, size) - 4, h = small + 8;
  int top = y + (line - h) / 2;
  rect(x + 1, top, w, h, {255, 255, 255}, 90 * alpha / 255, 4);
  rect(x + 2, top + 1, w - 2, h - 2, {12, 12, 14}, alpha, 3);
  label(x + 1 + w / 2, top + 3, t.badge, small, {222, 222, 224}, alpha, false, 0);
}

int View::measure(const std::string &s, int size, bool alt) {
  FT_Face f = alt && display ? display : face;
  FT_Set_Pixel_Sizes(f, 0, size);
  int pen = 0;
  for (size_t i = 0; i < s.size();) {
    size_t len;
    if (auto *t = buttonToken(s, i, len)) {
      pen += tokenWidth(t, size);
      FT_Set_Pixel_Sizes(f, 0, size);
      i += len;
      continue;
    }
    auto index = FT_Get_Char_Index(f, decode(s, i));
    if (!index)
      index = FT_Get_Char_Index(f, '?');
    pen += glyph(size, index, alt).advance;
  }
  return pen;
}

void View::label(int x, int y, const std::string &s, int size, Color color, int alpha, bool alt, int align, int maxWidth) {
  if (glyphBytes > 2 * 1024 * 1024 || glyphCache.size() > 2048) {
    glyphCache.clear();
    glyphBytes = 0;
  }
  std::string str = s;
  int w = measure(str, size, alt);
  if (maxWidth > 0 && w > maxWidth) {
    // Trim on character boundaries and add an ellipsis.
    while (!str.empty() && measure(str + "...", size, alt) > maxWidth) {
      // A button token goes whole.
      auto open = str.rfind('<');
      size_t len;
      if (str.back() == '>' && open != std::string::npos && buttonToken(str, open, len) && open + len == str.size()) {
        str.erase(open);
        continue;
      }
      str.pop_back();
      while (!str.empty() && (static_cast<unsigned char>(str.back()) & 0xc0) == 0x80)
        str.pop_back();
      if (!str.empty() && static_cast<unsigned char>(str.back()) >= 0xc0)
        str.pop_back();
    }
    str += "...";
    w = measure(str, size, alt);
  }
  FT_Face f = alt && display ? display : face;
  FT_Set_Pixel_Sizes(f, 0, size);
  int asc = f->size->metrics.ascender >> 6;
  int pen = align < 0 ? x : align == 0 ? x - w / 2 : x - w;
  int baseline = y + asc;
  for (size_t i = 0; i < str.size();) {
    size_t len;
    if (auto *t = buttonToken(str, i, len)) {
      drawToken(t, pen, y, size, alpha);
      pen += tokenWidth(t, size);
      FT_Set_Pixel_Sizes(f, 0, size);
      i += len;
      continue;
    }
    auto index = FT_Get_Char_Index(f, decode(str, i));
    if (!index)
      index = FT_Get_Char_Index(f, '?');
    const auto &g = glyph(size, index, alt);
    int gx = pen + g.left, gy = baseline - g.top;
    for (int row = 0; row < g.height; ++row) {
      int py = gy + row;
      if (py < 0 || py >= surfaceH)
        continue;
      for (int col = 0; col < g.width; ++col) {
        int px = gx + col;
        int a = g.mask[row * g.width + col] * alpha / 255;
        if (!a || px < 0 || px >= surfaceW)
          continue;
        over(&surface[(size_t(py) * surfaceW + px) * 4], color[0] * a / 255, color[1] * a / 255, color[2] * a / 255, a);
      }
    }
    pen += g.advance;
  }
}

// Layers: redraw into a transparent image only when the key changes.
bool View::beginLayer(Layer &layer, const std::string &key, int w, int h) {
  if (layer.key == key && layer.image.width == w && layer.image.height == h)
    return false;
  layer.key = key;
  if (layer.image.width != w || layer.image.height != h)
    layer.image = Image::blank(w, h);
  else
    std::fill(layer.image.rgba.begin(), layer.image.rgba.end(), 0);
  layer.image.touch();
  surface = layer.image.rgba.data();
  surfaceW = w;
  surfaceH = h;
  return true;
}
void View::endLayer() {
  surface = pixels.data();
  surfaceW = width;
  surfaceH = height;
}

void View::dropArt() {
  art.clear();
  heroOrder.clear();
  heroShown = heroFading = nullptr;
  heroKey.clear();
  fading = false;
  loader.reset();
}

void View::buildHome(const UI &u) {
  scene.clear();
  ++frameNumber;
  marqueeMoving = false;
  // The list view draws no pictures: none are kept, and nothing loads them.
  const bool simple = u.home.list();
  if (simple) {
    if (loader || !art.empty())
      dropArt();
  } else {
    if (!loader)
      loader.reset(new ArtLoader);
    loader->beginFrame();
    receiveArt(u);
  }
  // The blur preference changed: load every picture again.
  if (!simple && blurShown != u.blurAdult) {
    blurShown = u.blurAdult;
    for (auto &kv : art)
      u.artUpdated.push_back(kv.first);
  }
  // Downloaded art: forget what was decoded so the new pictures load. The
  // list keeps the names for when another view is shown.
  for (auto &k : simple ? std::vector<std::string>{} : u.artUpdated) {
    auto found = art.find(k);
    if (found != art.end()) {
      if (&found->second.hero == heroShown || &found->second.hero == heroFading) {
        heroShown = heroFading = nullptr;
        heroKey.clear();
      }
      heroOrder.erase(std::remove(heroOrder.begin(), heroOrder.end(), k), heroOrder.end());
      art.erase(found);
    }
    loader->rescan(k);
  }
  if (!simple)
    u.artUpdated.clear();
  loadBudget = 1;
  pendingLoads = coversToMake = false;
  auto list = u.homeList();
  int count = int(list.size());
  int target = std::min(u.home.target, count);
  const Entry *current = target < count ? &u.model.list[list[target]] : nullptr;
  bool settled = std::fabs(u.home.position - u.home.target) < 0.05 && !u.home.dragging;
  double details = u.home.view != Home::Carousel ? 0 : u.home.detailsAmount, side = u.home.sideAmount;
  auto quad = [&](const Image *img, double x, double y, double w, double h, double alpha = 1, double bright = 1, double radius = 0) {
    Quad q;
    q.image = img;
    q.x = float(x), q.y = float(y), q.w = float(w), q.h = float(h);
    q.alpha = float(alpha), q.bright = float(bright), q.radius = float(radius);
    scene.push_back(q);
    return &scene.back();
  };
  auto solid = [&](Color c, double x, double y, double w, double h, double alpha, double radius = 0) {
    auto *q = quad(nullptr, x, y, w, h, alpha, 1, radius);
    q->color = c;
    return q;
  };
  auto layerQuad = [&](const Layer &l, double x, double y, double alpha = 1) {
    if (!l.image.empty() && alpha > 0.004)
      quad(&l.image, x, y, l.image.width, l.image.height, alpha);
  };

  ensureStrips();

  // Background: the focused game's art, swapped only once the carousel settles.
  // The plain colour under it is added below, once it is known whether the
  // art covers the whole screen.
  size_t inkAt = scene.size();
  solid(ink, 0, 0, width, height, 1);
  std::string wantHero = u.home.view != Home::Carousel || !current ? "" : u.entryKey(*current);
  if (!simple && settled && wantHero != heroKey) {
    const Image *next = nullptr;
    bool ready = true;
    if (!wantHero.empty()) {
      auto &a = artFor(u, *current, 0, true);
      ready = a.heroLoaded;
      next = a.hero.empty() ? nullptr : &a.hero;
    }
    if (!ready)
      pendingLoads = true;
    if (ready) {
      heroFading = heroShown;
      heroShown = next;
      heroKey = wantHero;
      fadeStart = std::chrono::steady_clock::now();
      fading = true;
    }
  }
  double fade = fading ? std::chrono::duration<double>(std::chrono::steady_clock::now() - fadeStart).count() / 0.32 : 1;
  if (fade >= 1) {
    fading = false;
    heroFading = nullptr;
    fade = 1;
  }
  // Under an opaque background shown in full the plain colour is never seen.
  if (!heroFading && heroShown && fade >= 1 && opaque(heroShown))
    scene[inkAt].alpha = 0;
  if (heroFading)
    quad(heroFading, 0, 0, width, height, 1);
  else if (!heroShown || fade < 1)
    quad(&glow, 0, 0, width, height * 0.75, 1 - (heroShown ? fade : 0));
  if (heroShown)
    quad(heroShown, 0, 0, width, height, fade);
  if (u.home.grid())
    solid(ink, 0, 0, width, height, 0.82);
  else if (simple) {
    // Nothing: the plain colour and the glow are the whole background.
  } else {
    quad(&scrimV, 0, 0, width, height, 1 - details);
    quad(&scrimH, 0, 0, width, height, details);
  }
  quad(&tintTop, 0, 0, width, 70, 1);
  quad(&tintBottom, 0, height - 56, width, 56, 1);

  if (simple) {
    buildList(u, list, target);
  } else if (!u.home.grid()) {
    // Carousel: farther cards first so nearer ones overlap them.
    std::vector<std::pair<double, int>> order;
    for (int i = std::max(0, int(std::floor(u.home.position)) - 4); i <= std::min(count, int(std::ceil(u.home.position)) + 4); ++i)
      order.push_back({std::fabs(i - u.home.position), i});
    std::sort(order.rbegin(), order.rend());
    for (auto &o : order) {
      int i = o.second;
      auto r = card(i, u.home.position, details, side);
      if (r.x > width || r.x + r.w < 0)
        continue;
      // In details the cards right of the focused one fade away under the panel.
      double alpha = details > 0 && i > u.home.position + 0.5 ? 1 - details : 1;
      if (alpha <= 0.004)
        continue;
      if (i == count) {
        if (beginLayer(addLayer, std::string("add|") + languageCode(language()), 216, 120)) {
          rect(81, 0, 54, 54, white, 26, 27);
          blit(icon("plus-31"), 93, 12);
          label(108, 70, tr("Add games"), 17, {207, 207, 211}, 255, false, 0, 210);
          endLayer();
        }
        solid(white, r.x, r.y, r.w, r.h, 0.05 * alpha, 8);
        double k = r.w / 216.0;
        quad(&addLayer.image, r.x, r.y + r.h / 2 - 40 * k, 216 * k, 120 * k, alpha);
        continue;
      }
      auto &a = artFor(u, u.model.list[list[i]], o.first);
      solid({0, 0, 0}, r.x + 4, r.y + r.h - 4, r.w - 8, 10, 0.28 * alpha, 5);
      if (a.cover.empty())
        solid({28, 28, 31}, r.x, r.y, r.w, r.h, alpha, 8);
      else
        quad(&a.cover, r.x, r.y, r.w, r.h, alpha, 0.78 + 0.22 * r.nearness, 8);
    }
    // The ruby mark under the focused card, and the caption.
    if (details < 1) {
      auto r = card(target, u.home.position, details, side);
      double fadeOut = 1 - details;
      solid(ruby, r.x + r.w / 2 - 22, r.y + r.h + 7, 44, 3, fadeOut, 1);
      std::string title = current ? current->name : tr("Add games");
      bool fav = current && u.favourites.count(u.entryKey(*current));
      std::string sub = tr("Scan a folder on your memory card");
      if (current) {
        auto a = u.activity.find(u.entryKey(*current));
        sub = current->kind == Kind::Folder ? tr("Collection")
              : comingSoon(current->version) ? trf("RPG Maker {}: support is coming soon", {engineName(current->version)})
                                             : playtime(a == u.activity.end() ? 0 : a->second.seconds);
      }
      int maxW = side > 0.5 ? 440 : 860;
      // The layer is only as wide as its text: less to draw and upload.
      std::string key = title + "|" + sub + "|" + std::to_string(fav) + "|" + std::to_string(maxW);
      if (captionLayer.key != key) {
        int tw = std::min(maxW, measure(title, 30, true)) + (fav ? 26 : 0);
        int lw = std::max(tw, measure(sub, 15)) + 4;
        beginLayer(captionLayer, key, lw, 62);
        int tx = (lw - tw) / 2;
        if (fav)
          blitScaled(icon("star-24:ruby"), tx, 5, 21, 21);
        label(tx + (fav ? 26 : 0) + 1, 2, title, 30, {0, 0, 0}, 170, true, -1, maxW);
        label(tx + (fav ? 26 : 0), 0, title, 30, white, 255, true, -1, maxW);
        label(lw / 2, 37, sub, 15, muted, 255, false, 0);
        endLayer();
      }
      double cx = 480 + (sideShelfX - 480) * side;
      layerQuad(captionLayer, cx - captionLayer.image.width / 2.0, captionY, fadeOut);
    }
  } else {
    // Grid: five columns, the selected card lifted, names underneath.
    int rowH = cardH + gridLabel + gridGapY, visibleH = hintsY - gridTop;
    int want = (target / gridCols) * rowH - visibleH / 2 + rowH / 2;
    int maxScroll = std::max(0, (count / gridCols + 1) * rowH - visibleH);
    if (!u.home.pressed)
      u.gridScroll = std::max(0, std::min(want, maxScroll));
    const int clipTop = barY + barH + 4, clipBottom = hintsY - 6;
    for (int i = 0; i <= count; ++i) {
      int x = gridX + (i % gridCols) * (cardW + gridGapX), y = gridTop + (i / gridCols) * rowH - u.gridScroll;
      if (y + cardH + gridLabel < clipTop || y > clipBottom)
        continue;
      bool sel = i == target;
      double grow = sel ? 1.06 : 1.0;
      double w = cardW * grow, h = cardH * grow, gx = x - (w - cardW) / 2, gy = y - (h - cardH) / 2;
      size_t first = scene.size();
      if (i == count) {
        if (beginLayer(addLayer, std::string("add|") + languageCode(language()), 216, 120)) {
          rect(81, 0, 54, 54, white, 26, 27);
          blit(icon("plus-31"), 93, 12);
          label(108, 70, tr("Add games"), 17, {207, 207, 211}, 255, false, 0, 210);
          endLayer();
        }
        solid(white, gx, gy, w, h, 0.05, 8);
        quad(&addLayer.image, gx + (w - 216 * w / 216) / 2, gy + h / 2 - 40, w, 120 * w / 216);
      } else {
        auto &e = u.model.list[list[i]];
        auto &a = artFor(u, e, std::abs(i - target));
        if (a.cover.empty())
          solid({28, 28, 31}, gx, gy, w, h, 1, 8);
        else
          quad(&a.cover, gx, gy, w, h, 1, sel ? 1 : 0.85, 8);
        // Each name is a small cached label: selected (white, ruby rule) or not.
        bool fav = u.favourites.count(u.entryKey(e));
        std::string key = e.name + "|" + std::to_string(fav);
        if (a.labelKey != key) {
          a.labelKey = key;
          for (int s = 0; s < 2; ++s) {
            Layer l;
            beginLayer(l, key, cardW, gridLabel + 6);
            if (fav)
              blitScaled(icon("star-24:ruby"), 0, 3, 13, 13);
            label(fav ? 16 : 0, 2, e.name, 13, s ? white : muted, 255, false, -1, cardW - (fav ? 16 : 0));
            if (s)
              rect(0, gridLabel + 2, 30, 2, ruby, 255);
            endLayer();
            (s ? a.labelSelected : a.label) = std::move(l.image);
          }
        }
        const Image &lbl = sel ? a.labelSelected : a.label;
        quad(&lbl, x, y + cardH + 4, lbl.width, lbl.height);
      }
      for (size_t k = first; k < scene.size(); ++k) {
        scene[k].clipTop = clipTop;
        scene[k].clipBottom = clipBottom;
      }
    }
  }

  // Details panel, sliding in from the right over the darkened art.
  if (details > 0.01 && current) {
    auto &a = artFor(u, *current, 0);
    // Screenshots, four across along the bottom of the panel.
    if (!a.shotsLoaded) {
      ArtLoader::Request r;
      r.key = u.entryKey(*current);
      r.shots = true;
      r.root = u.model.paths.at("art/");
      r.names = u.artFolders(*current);
      r.cache = u.model.paths.at("cache/art/");
      r.width = shotW;
      r.height = shotH;
      r.blurAdult = u.blurAdult;
      loader->want(r, -1);
    }
    auto act = u.activity.find(u.entryKey(*current));
    Activity stat = act == u.activity.end() ? Activity{} : act->second;
    bool fav = u.favourites.count(u.entryKey(*current));
    std::string key = u.entryKey(*current) + "|" + std::to_string(fav) + "|" + std::to_string(stat.seconds) + "|" +
                      std::to_string(stat.last) + "|" + std::to_string(a.logo.id) + "|" + a.info + "|" +
                      std::to_string(a.shots.size()) + "|" + languageCode(language());
    if (beginLayer(detailsLayer, key, width - detailsX - 16, detailsBottom - detailsY)) {
      int w = width - detailsX - 40;
      // Facts downloaded with the artwork, blended from several sources.
      std::string released, developer, publisher, genres, description, sources;
      if (!a.info.empty())
        try {
          auto info = json5pp::parse(a.info);
          auto text = [&](const char *name) {
            auto &o = info.as_object();
            auto it = o.find(name);
            return it != o.end() && it->second.is_string() ? it->second.as_string() : std::string();
          };
          released = text("released");
          developer = text("developer");
          publisher = text("publisher");
          genres = text("genres");
          description = text("description");
          sources = text("sources");
        } catch (...) {
        }
      int y = 0;
      if (!a.logo.empty()) {
        blit(a.logo, 0, y);
        y += a.logo.height + 8;
      } else {
        label(0, y, current->name, 40, white, 255, true, -1, w);
        y += 52;
      }
      // Engine, year and studio on one line, then genres.
      std::string line = std::string("RPG Maker ") + engineName(current->version);
      for (auto *part : {&released, &developer})
        if (!part->empty())
          line += "  \xc2\xb7  " + *part;
      label(0, y, line, 15, muted, 255, false, -1, w - (fav ? 110 : 0));
      if (fav) {
        int fx = std::min(w - 110, measure(line, 15)) + 16;
        blitScaled(icon("star-24:ruby"), fx, y + 1, 16, 16);
        label(fx + 20, y, tr("Favourite"), 15, rubyHi);
      }
      y += 22;
      std::string credits = genres;
      if (!publisher.empty() && publisher != developer)
        credits += (credits.empty() ? "" : "  \xc2\xb7  ") + trf("Published by {}", {publisher});
      if (!credits.empty()) {
        label(0, y, credits, 13, faint, 255, false, -1, w);
        y += 20;
      }
      y += 12;
      struct Fact {
        std::string value, name;
      } facts[] = {{playtime(stat.seconds, false), tr("Play time")},
                   {lastPlayed(stat.last), tr("Last played")},
                   {current->kind == Kind::Zip ? "ZIP" : tr(current->kind == Kind::Folder ? "Collection" : "Folder"), tr("Source")},
                   {engineName(current->version), tr("Engine")}};
      int fx = 0;
      for (auto &f : facts) {
        label(fx, y, f.value, 23, white, 255, true);
        label(fx, y + 29, f.name, 12, faint);
        fx += std::max(110, measure(f.value, 23, true) + 34);
      }
      y += 58;
      // The folder line sits above the screenshots, or at the bottom.
      int by = (a.shots.empty() ? detailsBottom : shotY - 12) - detailsY;
      if (!description.empty()) {
        auto lines = wrap(description, 15, w);
        int room = std::max(0, (by - 28 - y) / 20);
        for (int i = 0; i < int(lines.size()) && i < room; ++i) {
          auto text = lines[i];
          if (i == room - 1 && i + 1 < int(lines.size()))
            text += "...";
          label(0, y + i * 20, text, 15, {208, 208, 212}, 255, false, -1, w);
        }
      }
      label(0, by - 20, sources.empty() ? current->node.path() : current->node.path() + "  \xc2\xb7  " + trf("details from {}", {sources}), 12, faint,
            255, false, -1, w);
      endLayer();
    }
    layerQuad(detailsLayer, detailsX + (1 - details) * 24, detailsY, details);
    // Play and Options, under the cover.
    if (beginLayer(buttonsLayer, std::string("buttons|") + languageCode(language()), optionsX + optionsW - playX, buttonH)) {
      int ox = optionsX - playX, iy = (buttonH - 30) / 2;
      rect(0, 0, playW, buttonH, ruby, 255, 7);
      blitScaled(icon("btn-cross-26"), 12, iy, 30, 30);
      label(50, (buttonH - 22) / 2, tr("Play"), 20, white, 255, false, -1, playW - 56);
      rect(ox, 0, optionsW, buttonH, white, 31, 7);
      blitScaled(icon("btn-triangle-26"), ox + 12, iy, 30, 30);
      label(ox + 50, (buttonH - 22) / 2, tr("Options"), 20, white, 255, false, -1, optionsW - 56);
      endLayer();
    }
    layerQuad(buttonsLayer, playX, playY + (1 - details) * 16, details);
    // The screenshots; the focused one is ringed. Each opens full screen.
    u.shotsShown = int(std::min<size_t>(4, a.shots.size()));
    for (int i = 0; i < u.shotsShown; ++i) {
      int sx = detailsX + int((1 - details) * 24) + i * (shotW + shotGap);
      u.shotHit[i][0] = sx, u.shotHit[i][1] = shotY, u.shotHit[i][2] = shotW, u.shotHit[i][3] = shotH;
      if (u.home.shotFocus == i)
        solid(ruby, sx - 3, shotY - 3, shotW + 6, shotH + 6, details, 6);
      Quad q;
      q.image = &a.shots[size_t(i)];
      q.x = float(sx);
      q.y = float(shotY);
      q.w = float(shotW), q.h = float(shotH), q.alpha = float(details), q.radius = 4;
      scene.push_back(q);
    }
  } else
    u.shotsShown = 0;

  // Top bar: brand on the left; About, clock, battery and settings on the right.
  topBar(u, u.home.zone == Home::Top ? u.home.topFocus : -1);

  // Toolbar: view switch and Add games on the left, engine tabs centred,
  // search and filter on the right.
  if (details < 0.99) {
    auto tabs = u.engineTabs();
    bool filtered = u.home.hiddenEngines || u.home.favouritesOnly || u.home.unplayedOnly || u.home.sort != Home::Recent || u.home.direction != 1;
    std::string key = std::string(languageCode(language())) + "|" + std::to_string(u.home.view) + "|" + std::to_string(u.engineFilter) + "|" + std::to_string(int(u.home.zone)) + "|" +
                      std::to_string(u.home.toolbarFocus) + "|" + std::to_string(filtered) + "|" + std::to_string(u.home.panel);
    for (int t : tabs)
      key += "," + std::to_string(t);
    if (beginLayer(toolbarLayer, key, width, barH)) {
      auto bar = [&](int item) { return u.home.zone == Home::Toolbar && u.home.toolbarFocus == item; };
      if (bar(0))
        rect(viewX, 0, barIcon, barH, white, 40, 25);
      // The view the button switches to: carousel, grid, list, round again.
      static const char *const nextIcon[] = {"view-grid-31", "view-list-31", "view-carousel-31"};
      blit(icon(nextIcon[u.home.view]), viewX + 10, 10);
      rect(addX, 0, addW, barH, white, bar(1) ? 56 : 28, 25);
      blit(icon("plus-31"), addX + 13, 10);
      // The button holds "Add games"; a language too long for it says "Add".
      std::string addText = tr("Add games");
      if (measure(addText, 19) > addW - 58)
        addText = tr("Add");
      label(addX + 51, 14, addText, 19, {238, 238, 240}, 255, false, -1, addW - 58);
      // Tabs size to their names and sit between Add games and search. When
      // more engines exist than fit, a window of them slides to keep the
      // selected one in view, with a fade at the cut edge.
      const int left = addX + addW + 40, right = searchX - 40;
      std::vector<int> widths;
      size_t selected = 0;
      for (size_t i = 0; i < tabs.size(); ++i) {
        widths.push_back(measure(engineTab(tabs[i]), 23, true) + tabPad);
        if (tabs[i] == u.engineFilter)
          selected = i;
      }
      size_t first = 0, last = tabs.size();
      auto span = [&](size_t a, size_t b) {
        int w = 0;
        for (size_t i = a; i < b; ++i)
          w += widths[i];
        return w;
      };
      while (span(first, last) > right - left) {
        // Drop from whichever end is farther from the selected tab.
        if (selected - first > last - 1 - selected)
          ++first;
        else
          --last;
      }
      int total = span(first, last);
      int x = (left + right) / 2 - total / 2;
      u.tabHit.clear();
      label(x - 24, 16, "L", 13, faint);
      for (size_t i = first; i < last; ++i) {
        bool on = tabs[i] == u.engineFilter;
        if (on)
          rect(x + 3, 5, widths[i] - 6, tabH, white, 33, 20);
        label(x + widths[i] / 2, 10, engineTab(tabs[i]), 23, on ? white : muted, 255, true, 0);
        u.tabHit.push_back({x, widths[i], tabs[i]});
        x += widths[i];
      }
      if (first > 0)
        label(x - total - 40, 12, "\xe2\x80\xb9", 19, muted);
      if (last < tabs.size())
        label(x + 30, 12, "\xe2\x80\xba", 19, muted);
      label(x + 13, 16, "R", 13, faint);
      if (bar(2))
        rect(searchX, 0, barIcon, barH, white, 40, 25);
      blit(icon("search-31"), searchX + 10, 10);
      if (bar(3) || u.home.panel == Home::Filter)
        rect(filterX, 0, barIcon, barH, white, 40, 25);
      blit(icon("filter-31"), filterX + 10, 10);
      if (filtered)
        rect(filterX + 35, 8, 9, 9, rubyHi, 255, 4);
      endLayer();
    } else {
      // Keep touch in step with the drawn tabs.
      if (u.tabHit.empty())
        toolbarLayer.key.clear();
    }
    layerQuad(toolbarLayer, 0, barY, 1 - details);
  }

  if (u.showFrameTime) {
    if (beginLayer(frameLayer, u.frameText, 420, 22)) {
      rect(0, 0, 420, 22, {0, 0, 0}, 160, 6);
      label(8, 3, u.frameText.empty() ? tr("Measuring...") : u.frameText, 13, {120, 230, 160});
      endLayer();
    }
    layerQuad(frameLayer, 270, 20);
  }
  homePanels(u, list);
  // A screenshot full screen over everything but the hints.
  if (u.home.viewing >= 0 && current) {
    auto &a = artFor(u, *current, 0);
    int n = u.home.viewing + 1;
    if (a.viewedShot != n) {
      ArtLoader::Request r;
      r.key = u.entryKey(*current);
      r.shots = true;
      r.shot = n;
      r.root = u.model.paths.at("art/");
      r.names = u.artFolders(*current);
      r.cache = u.model.paths.at("cache/art/");
      r.width = width - 40;
      r.height = hintsY - 24;
      r.blurAdult = u.blurAdult;
      loader->want(r, -2);
    }
    solid({0, 0, 0}, 0, 0, width, height, 0.97);
    if (a.viewedShot == n && !a.viewed.empty()) {
      double w = a.viewed.width, h = a.viewed.height;
      quad(&a.viewed, (width - w) / 2, (hintsY - h) / 2, w, h, 1, 1, 6);
    }
    // Arrows at the sides when there is more than one.
    if (u.shotsShown > 1) {
      if (beginLayer(viewerLayer, "viewer-arrows", width, 60)) {
        label(30, 10, "<", 40, white, 200, true, 0);
        label(width - 30, 10, ">", 40, white, 200, true, 0);
        endLayer();
      }
      layerQuad(viewerLayer, 0, hintsY / 2 - 30);
    }
  }
  quad(&band, 0, hintsY - 4, width, height - (hintsY - 4), 1);
  homeHints(u, list);

  if (!u.message.empty()) {
    if (beginLayer(messageLayer, u.message, width, 40)) {
      int w = std::min(880, measure(u.message, 15) + 34);
      rect(480 - w / 2, 0, w, 36, {22, 22, 24}, 245, 18);
      label(480, 9, u.message, 15, {230, 230, 232}, 255, false, 0, 860);
      endLayer();
    }
    layerQuad(messageLayer, 0, 116);
  }
  if (!u.launchingName.empty()) {
    solid({0, 0, 0}, 0, 0, width, height, 0.8);
    if (beginLayer(messageLayer, "launch|" + u.launchingName + "|" + u.preparationLabel, width, 90)) {
      label(480, 0, trf("Starting {}", {u.launchingName}), 28, white, 255, true, 0, 900);
      label(480, 44, u.preparationLabel.empty() ? tr("Loading") : u.preparationLabel, 16, muted, 255, false, 0, 900);
      endLayer();
    }
    layerQuad(messageLayer, 0, 232);
    if (u.preparationFraction >= 0) {
      solid(white, 280, 318, 400, 6, 0.14, 3);
      solid(ruby, 280, 318, 400 * std::min(1.0, u.preparationFraction), 6, 1, 3);
    }
  }
  if (loader) {
    pendingLoads = pendingLoads || loader->busy();
    trimArt();
  }
}

// The selected name slides left after a second, holds at its end, then goes
// back; three times, then it rests cut, so an idle screen stops redrawing.
// Only the quad moves: the whole name is drawn once. Returns false while the
// cut name is shown instead.
template <class AddQuad>
bool View::marquee(const std::string &id, const std::string &name, int maxW, int x, double y, int h, AddQuad &&quad) {
  const double delay = 1.0, speed = 45, hold = 1.5, rest = 0.8;
  auto now = std::chrono::steady_clock::now();
  // Selected again (or anew): it starts over.
  if (marqueeId != id || marqueeFrame + 1 != frameNumber) {
    marqueeId = id;
    marqueeStart = now;
  }
  marqueeFrame = frameNumber;
  std::string key = name + "|" + languageCode(language());
  if (marqueeLayer.key != key) {
    int full = std::min(2040, measure(name, 19));
    beginLayer(marqueeLayer, key, full + 4, h);
    label(0, 2, name, 19, white, 255, false, -1, full + 4);
    endLayer();
  }
  double travel = std::max(0, marqueeLayer.image.width - 4 - maxW);
  double cycle = delay + travel / speed + hold + rest;
  double t = std::chrono::duration<double>(now - marqueeStart).count();
  marqueeMoving = t < 3 * cycle;
  if (!marqueeMoving)
    return false;
  t = std::fmod(t, cycle);
  if (t < delay || t >= cycle - rest)
    return false;
  double offset = std::min(travel, (t - delay) * speed);
  auto *q = quad(&marqueeLayer.image, x - offset, y, marqueeLayer.image.width, h, 1, 1);
  q->clipLeft = x;
  q->clipRight = x + maxW;
  return true;
}

void View::buildList(const UI &u, const std::vector<size_t> &list, int target) {
  // Rows are quads: moving the selection only moves a highlight, and a name
  // is drawn once, when its row first shows.
  int count = int(list.size()), total = count + 1; // the last row adds games
  int maxScroll = std::max(0, (total - listRows) * listRowH);
  // The UI keeps the selection in view (UI::followList); a drag scrolls freely.
  u.listScroll = std::max(0, std::min(u.listScroll, maxScroll));
  auto quad = [&](const Image *img, double x, double y, double w, double h, double alpha = 1, double bright = 1, double radius = 0) {
    Quad q;
    q.image = img;
    q.x = float(x), q.y = float(y), q.w = float(w), q.h = float(h);
    q.alpha = float(alpha), q.bright = float(bright), q.radius = float(radius);
    q.clipTop = listTop;
    q.clipBottom = listBottom;
    scene.push_back(q);
    return &scene.back();
  };
  const int nameX = listX + 16, tagRight = listX + listW - 14, nameH = 26;
  const std::string lang = languageCode(language());
  int first = u.listScroll / listRowH, last = std::min(total - 1, (u.listScroll + listRows * listRowH) / listRowH);
  for (int i = first; i <= last; ++i) {
    double y = listTop + i * listRowH - u.listScroll;
    bool sel = i == target;
    if (sel) {
      quad(nullptr, listX, y + 2, listW, listRowH - 4, 0.1, 1, 6)->color = white;
      quad(nullptr, listX, y + 8, 3, listRowH - 16, 1)->color = ruby;
    }
    if (i == count) {
      if (beginLayer(listAddLayer, "add|" + lang, 300, nameH)) {
        blitScaled(icon("plus-31"), 0, 3, 20, 20);
        label(30, 2, tr("Add games"), 19, white);
        endLayer();
      }
      quad(&listAddLayer.image, nameX, y + (listRowH - nameH) / 2.0, listAddLayer.image.width, nameH, 1, sel ? 1 : 0.7);
      continue;
    }
    auto &e = u.model.list[list[size_t(i)]];
    auto id = u.entryKey(e);
    bool fav = u.favourites.count(id);
    // The engine, small and right-aligned, from five shared layers.
    int v = e.version >= 0 && e.version <= formatMZ ? e.version : 0;
    Layer &tag = listTagLayers[v];
    // MV and MZ games say they cannot run yet.
    std::string tagText = !v ? std::string(tr("Collection"))
                        : comingSoon(v) ? trf("{}, coming soon", {engineName(v)})
                                        : std::string(engineName(v));
    if (beginLayer(tag, lang, measure(tagText, 14) + 4, 20)) {
      label(0, 1, tagText, 14, comingSoon(v) ? rubyHi : faint);
      endLayer();
    }
    quad(&tag.image, tagRight - tag.image.width, y + (listRowH - 20) / 2.0, tag.image.width, 20);
    // A favourite's star sits in the margin left of the row, so every name
    // starts in the same place.
    if (fav) {
      const Image &star = icon("star-24:ruby");
      quad(&star, listX - 24, y + (listRowH - 18) / 2.0, 18, 18);
    }
    // The name.
    int maxW = listW - 16 - 150;
    auto &row = listNames[id];
    row.used = frameNumber;
    std::string key = e.name + "|" + lang;
    if (row.layer.key != key) {
      int full = measure(e.name, 19);
      row.cut = full > maxW;
      beginLayer(row.layer, key, std::min(maxW, full) + 4, nameH);
      label(0, 2, e.name, 19, white, 255, false, -1, maxW);
      endLayer();
    }
    double ny = y + (listRowH - nameH) / 2.0;
    if (sel && row.cut && marquee(id, e.name, maxW, nameX, ny, nameH, quad))
      continue;
    quad(&row.layer.image, nameX, ny, row.layer.image.width, nameH, 1, sel ? 1 : 0.72);
  }
  // A thin scroll bar when there are more rows than fit.
  if (total > listRows) {
    double trackH = listRows * listRowH, thumbH = std::max(24.0, trackH * listRows / total);
    double thumbY = listTop + (trackH - thumbH) * u.listScroll / std::max(1, maxScroll);
    quad(nullptr, listX + listW + 10, listTop, 3, trackH, 0.08, 1, 1)->color = white;
    quad(nullptr, listX + listW + 10, thumbY, 3, thumbH, 0.45, 1, 1)->color = white;
  }
  // Names not drawn this frame are forgotten once there are many.
  if (listNames.size() > 60)
    for (auto it = listNames.begin(); it != listNames.end();)
      it = it->second.used == frameNumber ? std::next(it) : listNames.erase(it);
}

void View::homePanels(const UI &u, const std::vector<size_t> &list) {
  auto layerQuad = [&](const Layer &l, double x, double y) {
    Quad q;
    q.image = &l.image;
    q.x = float(x), q.y = float(y), q.w = float(l.image.width), q.h = float(l.image.height);
    scene.push_back(q);
  };
  if (u.home.panel == Home::Artwork) {
    // The artwork chooser covers the screen below the top bar: a row per
    // kind of picture, each source's version side by side.
    Quad dim;
    dim.y = float(topHeight), dim.w = float(width), dim.h = float(hintsY - topHeight), dim.alpha = 0.97f;
    dim.color = {12, 12, 14};
    scene.push_back(dim);
    std::string key = "artwork|" + u.artChoices.key + "|" + std::to_string(u.artRow) + "|" + languageCode(language());
    for (int k = 0; k < 3; ++k)
      key += "|" + std::to_string(u.artPick[k]);
    if (beginLayer(panelLayer, key, width, hintsY - topHeight)) {
      label(30, 12, tr("Choose artwork"), 26, white, 255, true);
      label(30 + measure(tr("Choose artwork"), 26, true) + 14, 20, u.artChoices.name, 15, muted, 255, false, -1, 560);
      const char *rowNames[] = {"Cover", "Background", "Logo"};
      for (int k = 0; k < 3; ++k) {
        auto &o = u.artChoices.options[k];
        int x, y, w, h;
        pickerSlot(k, 0, x, y, w, h);
        y -= topHeight;
        label(30, y + h / 2 - 9, tr(rowNames[k]), 17, u.artRow == k ? white : muted, 255, true, -1, pickerX - 38);
        if (u.artRow == k)
          rect(30, y + h / 2 + 14, 24, 3, ruby, 255, 1);
        if (o.empty())
          label(x, y + h / 2 - 8, tr("Nothing found"), 14, faint);
        for (int i = 0; i < int(o.size()); ++i) {
          pickerSlot(k, i, x, y, w, h);
          y -= topHeight;
          bool picked = u.artPick[k] == i;
          if (picked)
            rect(x - 4, y - 4, w + 8, h + 8, white, u.artRow == k ? 230 : 110, 9);
          label(x + w / 2, y + h + 4, tr(o[i].source), 12, picked ? white : muted, 255, false, 0);
        }
      }
      endLayer();
    }
    // Thumbnails are quads over the frame rings drawn in the layer.
    layerQuad(panelLayer, 0, topHeight);
    for (int k = 0; k < 3; ++k)
      for (int i = 0; i < int(u.artChoices.options[k].size()); ++i) {
        auto &t = u.artChoices.options[k][i].thumb;
        int x, y, w, h;
        pickerSlot(k, i, x, y, w, h);
        Quad q;
        q.image = &t;
        q.radius = 6;
        if (k == 2 && !t.empty()) {
          // Logos keep their shape, centred in the slot.
          double s = std::min(double(w) / t.width, double(h) / t.height);
          q.w = float(t.width * s), q.h = float(t.height * s);
          q.x = float(x + (w - q.w) / 2), q.y = float(y + (h - q.h) / 2), q.radius = 0;
          Quad back;
          back.x = float(x), back.y = float(y), back.w = float(w), back.h = float(h), back.alpha = 0.06f, back.radius = 6;
          back.color = {255, 255, 255};
          scene.push_back(back);
        } else
          q.x = float(x), q.y = float(y), q.w = float(w), q.h = float(h);
        scene.push_back(q);
      }
    return;
  }
  if (u.home.panel == Home::Options) {
    auto *e = u.homeEntry();
    if (!e)
      return;
    bool fav = u.favourites.count(u.entryKey(*e));
    std::string key = "options|" + u.entryKey(*e) + "|" + std::to_string(fav) + "|" + std::to_string(u.home.panelFocus) + "|" + languageCode(language());
    if (beginLayer(panelLayer, key, menuW, 30 + 5 * menuRowH + 8)) {
      rect(0, 0, menuW, 30 + 5 * menuRowH + 8, {22, 22, 24}, 248, 12);
      label(14, 9, e->name, 12, faint, 255, false, -1, menuW - 28);
      const char *rows[] = {fav ? "Remove from favourites" : "Add to favourites", "Change artwork and info", "Game info for all games",
                            "Game settings", "Rename"};
      for (int i = 0; i < 5; ++i) {
        int y = 30 + i * menuRowH;
        if (u.home.panelFocus == i)
          rect(6, y, menuW - 12, menuRowH - 4, white, 30, 8);
        if (i == 0)
          blitScaled(icon(fav ? "star-24:ruby" : "star-24"), 18, y + 12, 17, 17);
        label(44, y + 11, tr(rows[i]), 16, {228, 228, 230}, 255, false, -1, menuW - 56);
      }
      endLayer();
    }
    layerQuad(panelLayer, menuX, menuY);
    return;
  }
  if (u.home.panel != Home::Filter)
    return;
  // Filter: a half-width panel under its icon, running down to the hints.
  int favs = 0, unplayed = 0, perEngine[5] = {0, 0, 0, 0, 0};
  for (auto &e : u.model.list) {
    favs += u.favourites.count(u.entryKey(e)) ? 1 : 0;
    auto a = u.activity.find(u.entryKey(e));
    unplayed += a == u.activity.end() || !a->second.seconds;
    if (e.version >= 0 && e.version <= 4)
      ++perEngine[e.version];
  }
  std::string key = "filter|" + std::to_string(list.size()) + "|" + std::to_string(u.home.hiddenEngines) + "|" + std::to_string(u.home.favouritesOnly) + "|" +
                    std::to_string(u.home.unplayedOnly) + "|" + std::to_string(u.home.sort) + "|" + std::to_string(u.home.direction) + "|" +
                    std::to_string(u.home.panelFocus) + "|" + std::to_string(favs) + "|" + std::to_string(unplayed) + "|" + languageCode(language());
  int ph = hintsY - panelY;
  if (beginLayer(panelLayer, key, panelW, ph)) {
    rect(0, 0, panelW, ph, {14, 14, 16}, 248, 0);
    int x = 18, y = 12, inner = panelW - 36;
    label(x, y, tr("Filter"), 26, white, 255, true);
    label(x, y + 30, trf("Showing {} of {} games", {std::to_string(list.size()), std::to_string(u.model.list.size())}), 13, muted);
    const int reset = u.filterRows() - 1;
    auto focusRing = [&](int row, int rx, int ry, int rw, int rh) {
      u.filterHit[row][0] = panelX + rx;
      u.filterHit[row][1] = panelY + ry;
      u.filterHit[row][2] = rw;
      u.filterHit[row][3] = rh;
      if (u.home.panelFocus == row) {
        rect(rx, ry, rw, 2, white, 200);
        rect(rx, ry + rh - 2, rw, 2, white, 200);
        rect(rx, ry, 2, rh, white, 200);
        rect(rx + rw - 2, ry, 2, rh, white, 200);
      }
    };
    bool any = u.home.hiddenEngines || u.home.favouritesOnly || u.home.unplayedOnly || u.home.sort != Home::Recent || u.home.direction != 1;
    rect(x + inner - 84, y + 2, 84, 42, white, 26, 8);
    label(x + inner - 42, y + 13, tr("Reset"), 15, any ? white : faint, 255, false, 0, 80);
    focusRing(reset, x + inner - 84, y + 2, 84, 42);
    auto section = [&](int sy, const char *english) {
      const char *name = tr(english);
      label(x, sy, name, 12, faint);
      rect(x + measure(name, 12) + 8, sy + 8, inner - measure(name, 12) - 8, 1, white, 20);
    };
    auto tile = [&](int row, int tx, int ty, int tw, bool on, bool off, const std::string &title, const std::string &sub, const char *iconName) {
      rect(tx, ty, tw, 46, white, on ? 36 : 15, 9);
      int lx = tx + 12;
      if (iconName) {
        blit(icon(iconName), tx + 10, ty + 11, on ? 255 : 150);
        lx = tx + 42;
      }
      label(lx, ty + (!sub.empty() && iconName ? 6 : 12), title, 18, on ? white : off ? faint : Color{207, 207, 211}, 255, true, -1,
            tx + tw - lx - (!sub.empty() && !iconName ? measure(sub, 11) + 16 : 6));
      if (!sub.empty() && !iconName)
        label(tx + tw - 10, ty + 17, sub, 11, faint, 255, false, 1);
      if (!sub.empty() && iconName)
        label(lx, ty + 27, sub, 11, {208, 208, 212}, 255, false, -1, tx + tw - lx - 4);
      if (on && !iconName)
        rect(tx + 12, ty + 40, 22, 3, ruby, 255, 1);
      if (on && iconName)
        rect(tx, ty, 3, 46, ruby, 255);
      focusRing(row, tx, ty, tw, 46);
    };
    int sy = y + 56;
    section(sy, "Order by");
    const char *sortNames[] = {"Last played", "Name", "Play time"};
    const char *sortIcons[] = {"sort-recent-24", "sort-name-24", "sort-time-24"};
    const char *down[] = {"Newest first", "A to Z", "Most first"}, *up[] = {"Oldest first", "Z to A", "Least first"};
    int tw3 = (inner - 12) / 3;
    for (int i = 0; i < Home::SortCount; ++i) {
      bool on = u.home.sort == i;
      tile(6 + i, x + i * (tw3 + 6), sy + 20, tw3, on, false, tr(sortNames[i]),
           on ? std::string(u.home.direction > 0 ? "\xe2\x86\x93 " : "\xe2\x86\x91 ") + tr(u.home.direction > 0 ? down[i] : up[i]) : "", sortIcons[i]);
    }
    sy += 82;
    section(sy, "Engines");
    int tw2 = (inner - 6) / 2;
    for (int i = 0; i < 4; ++i) {
      int f = i + 1, n = perEngine[f];
      bool hidden = u.home.hiddenEngines & (1u << f);
      tile(i, x + (i % 2) * (tw2 + 6), sy + 20 + (i / 2) * 52, tw2, !hidden, hidden, engineName(f),
           n == 1 ? std::string(tr("1 game")) : trf("{} games", {std::to_string(n)}), nullptr);
    }
    sy += 134;
    section(sy, "Only show");
    tile(4, x, sy + 20, tw2, u.home.favouritesOnly, false, std::string("     ") + tr("Favourites"),
         favs == 1 ? std::string(tr("1 game")) : trf("{} games", {std::to_string(favs)}), nullptr);
    blitScaled(icon("star-24:ruby"), x + 12, sy + 35, 16, 16);
    tile(5, x + tw2 + 6, sy + 20, tw2, u.home.unplayedOnly, false, tr("Not played"),
         unplayed == 1 ? std::string(tr("1 game")) : trf("{} games", {std::to_string(unplayed)}), nullptr);
    endLayer();
  }
  Quad shade;
  shade.x = panelX - 3, shade.y = panelY, shade.w = 3, shade.h = float(ph), shade.alpha = 0.47f;
  scene.push_back(shade);
  layerQuad(panelLayer, panelX, panelY);
}

void View::homeHints(const UI &u, const std::vector<size_t> &list) {
  std::vector<Hint> left, right;
  bool add = u.home.target >= int(list.size());
  if (u.home.panel == Home::Artwork) {
    if (u.lastArtRow())
      left = {{"btn-cross-26", tr("Save"), Button::Cross}, {"btn-circle-26", tr("Keep current"), Button::Circle}};
    else
      left = {{"btn-cross-26", tr("Next"), Button::Cross}, {"btn-triangle-26", tr("Save"), Button::Triangle},
              {"btn-circle-26", tr("Keep current"), Button::Circle}};
    right = {{nullptr, tr("Pick with the D-pad or a tap"), Button::None}};
  } else if (u.home.panel != Home::NoPanel) {
    left = {{"btn-cross-26", tr("Select"), Button::Cross}, {"btn-circle-26", tr("Close"), Button::Circle}};
  } else if (u.home.viewing >= 0) {
    left = {{"btn-circle-26", tr("Close"), Button::Circle}};
    if (u.shotsShown > 1)
      right = {{nullptr, tr("Left and Right: more screenshots"), Button::None}};
    hintBar(u, left, right, std::to_string(u.home.viewing + 1) + "|" + std::to_string(std::max(1, u.shotsShown)));
    return;
  } else if (u.home.details && u.home.shotFocus >= 0) {
    left = {{"btn-cross-26", tr("View"), Button::Cross}, {"btn-circle-26", tr("Back"), Button::Circle}};
    right = {{nullptr, tr("Up: game details"), Button::None}};
  } else if (u.home.details) {
    left = {{"btn-cross-26", tr("Play"), Button::Cross}, {"btn-circle-26", tr("Back"), Button::Circle}, {"btn-triangle-26", tr("Options"), Button::Triangle}};
    right = {{nullptr, tr(u.shotsShown ? "Down: screenshots" : "Other games"), Button::None}};
  } else {
    left = {{"btn-cross-26", tr(add ? "Add games" : u.home.view != Home::Carousel ? "Play" : "Details"), Button::Cross}};
    if (!add)
      left.push_back({"btn-triangle-26", tr("Options"), Button::Triangle});
    right = {{"L R", tr("Engine"), Button::R}, {"SELECT", tr("Search"), Button::Select}};
  }
  std::string counter = u.home.panel == Home::NoPanel ? (add ? "+" : std::to_string(u.home.target + 1)) + "|" + std::to_string(list.size()) : "";
  hintBar(u, left, right, counter);
}

// Strip gradients shared by every screen, built once and stretched by quads.
bool View::opaque(const Image *img) {
  auto found = opaqueHeroes.find(img->id);
  if (found != opaqueHeroes.end())
    return found->second;
  bool solidAll = !img->empty();
  for (size_t i = 3; solidAll && i < img->rgba.size(); i += 4 * 7)
    solidAll = img->rgba[i] == 255;
  if (opaqueHeroes.size() > 32)
    opaqueHeroes.clear();
  opaqueHeroes[img->id] = solidAll;
  return solidAll;
}

void View::ensureStrips() {
  if (!scrimV.empty())
    return;
  {
    scrimV = strip(1, height, [](double t) {
      double b = 1 - t; // 0 at the bottom
      double a = b < 0.09 ? 1.0 : b < 0.42 ? 1.0 - 0.45 * (b - 0.09) / 0.33 : b < 0.70 ? 0.55 - 0.2 * (b - 0.42) / 0.28 : 0.35 + 0.35 * (b - 0.70) / 0.30;
      return std::array<int, 4>{{7, 7, 7, int(a * 255)}};
    });
    scrimH = strip(width, 1, [](double t) {
      double a = t < 0.36 ? 0.45 + 0.35 * t / 0.36 : 0.8 + 0.13 * (t - 0.36) / 0.64;
      return std::array<int, 4>{{7, 7, 7, int(a * 255)}};
    });
    tint = strip(1, height, [](double t) {
      double y = t * height;
      int a = y < 70 ? int((70 - y) * 82 / 70) : y >= height - 56 ? int((y - (height - 56)) * 72 / 56) : 0;
      return std::array<int, 4>{{150, 14, 38, a}};
    });
    tintTop = strip(1, 70, [](double t) { return std::array<int, 4>{{150, 14, 38, int((1 - t) * 82)}}; });
    tintBottom = strip(1, 56, [](double t) { return std::array<int, 4>{{150, 14, 38, int(t * 72)}}; });
    band = strip(1, 64, [](double t) { return std::array<int, 4>{{7, 7, 7, int(std::min(1.0, t * 1.6) * 205)}}; });
    glow = Image::blank(64, 36);
    for (int y = 0; y < 36; ++y)
      for (int x = 0; x < 64; ++x) {
        double dx = (x - 32) / 32.0, dy = (y - 12) / 24.0;
        int a = int(std::max(0.0, 1 - std::sqrt(dx * dx + dy * dy)) * 34);
        uint8_t *p = &glow.rgba[(size_t(y) * 64 + x) * 4];
        p[0] = p[1] = p[2] = a;
        p[3] = a;
      }
  }
}

// The top bar: brand, About, clock, battery and settings. focus rings the
// About icon (0) or the gear (1).
void View::topBar(const UI &u, int focus) {
  {
    bool topFocus = focus >= 0;
    std::string key = u.clockText + "|" + std::to_string(u.batteryPercent) + "|" + std::to_string(focus) + "|" +
                      std::to_string(overlay);
    if (beginLayer(topLayer, key, width, topHeight)) {
      blit(icon("emblem-40"), 16, 11);
      blit(icon("wordmark-25"), 64, 19);
      label(64 + icon("wordmark-25").width + 7, 29, homeVersion, 12, faint);
      if (topFocus)
        rect(focus ? gearX : aboutX, topIconY, topIcon, topIcon, white, 36, 27);
      // Over a running game the About and settings buttons do nothing.
      if (!overlay)
        blit(icon("info-35"), aboutX + 12, topIconY + 12);
      int right = gearX - 8;
      if (u.batteryPercent >= 0) {
        int bx = right - 52, by = 18;
        rect(bx, by, 46, 26, {230, 230, 232}, 255, 5);
        rect(bx + 1, by + 1, 44, 24, {20, 20, 22}, 255, 4);
        rect(bx + 3, by + 3, 40 * u.batteryPercent / 100, 20, u.batteryPercent > 20 ? Color{31, 138, 98} : ruby, 255, 3);
        rect(bx + 47, by + 9, 3, 8, {230, 230, 232}, 255, 1);
        label(bx + 23, by + 4, std::to_string(u.batteryPercent), 15, white, 255, false, 0);
        right = bx - 12;
      }
      if (!u.clockText.empty())
        label(right, 16, u.clockText, 24, {232, 232, 234}, 255, false, 1);
      if (!overlay)
        blit(icon("gear-35"), gearX + 12, topIconY + 12);
      endLayer();
    }
    Quad q;
    q.image = &topLayer.image;
    q.w = float(width), q.h = float(topHeight);
    scene.push_back(q);
  }

}

// The hint bar along the bottom: button glyphs with what they do (each one
// tappable), extra notes on the right and a position counter in the middle.
void View::hintBar(const UI &u, const std::vector<Hint> &left, const std::vector<Hint> &right, const std::string &counter) {
  std::string key = counter;
  for (auto &h : left)
    key += "|" + h.text;
  for (auto &h : right)
    key += "|" + h.text;
  if (beginLayer(hintsLayer, key, width, height - hintsY)) {
    u.hintHit.clear();
    int x = 14;
    for (auto &h : left) {
      blit(icon(h.glyph), x, 8);
      label(x + 32, 12, h.text, 15, {214, 214, 216});
      int w = 32 + measure(h.text, 15) + 18;
      u.hintHit.push_back({x - 4, w, h.button});
      x += w;
    }
    int rx = width - 14;
    for (auto it = right.rbegin(); it != right.rend(); ++it) {
      int tw = measure(it->text, 15);
      rx -= tw;
      label(rx, 12, it->text, 15, {214, 214, 216});
      if (it->glyph) {
        std::string badge = it->glyph;
        int bw = measure(badge, 11) + 13;
        rx -= bw + 8;
        rect(rx, 10, bw, 21, white, 90, 4);
        rect(rx + 1, 11, bw - 2, 19, {12, 12, 14}, 255, 3);
        label(rx + bw / 2, 13, badge, 11, {222, 222, 224}, 255, false, 0);
        u.hintHit.push_back({rx - 4, bw + tw + 16, it->button});
      }
      rx -= 18;
    }
    if (!counter.empty()) {
      std::string pos = counter.substr(0, counter.find('|')), total = " / " + counter.substr(counter.find('|') + 1);
      int w = measure(pos, 15) + measure(total, 15);
      label(480 - w / 2, 12, pos, 15, {232, 232, 234});
      label(480 - w / 2 + measure(pos, 15), 12, total, 15, faint);
    }
    endLayer();
  }
  Quad q;
  q.image = &hintsLayer.image;
  q.x = 0, q.y = float(hintsY), q.w = float(width), q.h = float(height - hintsY);
  scene.push_back(q);
}


// Desktop and snapshots: draw the scene on the CPU, as the GPU would.
void View::composite() {
  surface = pixels.data();
  surfaceW = width;
  surfaceH = height;
  for (auto &q : scene) {
    // Clip by drawing into a window of rows.
    int top = std::max(0, q.clipTop), bottom = std::min(height, q.clipBottom);
    if (top >= bottom)
      continue;
    surface = pixels.data() + size_t(top) * width * 4;
    surfaceH = bottom - top;
    // Columns outside the clip are put back as they were after drawing.
    int left = std::max(0, q.clipLeft), right = std::min(width, q.clipRight);
    if (left >= right)
      continue;
    std::vector<uint8_t> keep;
    bool columns = left > 0 || right < width;
    if (columns)
      keep.assign(surface, surface + size_t(surfaceH) * width * 4);
    if (q.image)
      blitScaled(*q.image, q.x, q.y - top, q.w, q.h, int(q.alpha * 255 + 0.5f), int(q.bright * 256 + 0.5f), int(q.radius));
    else
      rect(int(std::lround(q.x)), int(std::lround(q.y)) - top, int(std::lround(q.w)), int(std::lround(q.h)), q.color,
           int(q.alpha * 255 + 0.5f), int(q.radius));
    if (columns)
      for (int row = 0; row < surfaceH; ++row) {
        uint8_t *d = surface + size_t(row) * width * 4;
        const uint8_t *s = keep.data() + size_t(row) * width * 4;
        std::copy(s, s + left * 4, d);
        std::copy(s + right * 4, s + width * 4, d + right * 4);
      }
  }
  surface = pixels.data();
  surfaceH = height;
}
} // namespace hardrpg
