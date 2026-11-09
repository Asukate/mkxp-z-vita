// The in-game keyboard's pictures: frosted glass keys with a bright rim,
// drawn once per page and modifier state. The blur behind the glass is the
// GPU's (display/game-menu-layer.h); these images hold the light tint, the
// rims, the labels and the icons.
#include "input/vita-keyboard.h"
#include "view.h"
#include <algorithm>
#include <cmath>

namespace hardrpg {
namespace {
const std::array<int, 3> ink{{246, 247, 250}};
constexpr int inkAlpha = 245;

inline void blend(uint8_t *d, const std::array<int, 3> &c, int a) {
  if (a <= 0)
    return;
  a = std::min(a, 255);
  d[0] = uint8_t(c[0] * a / 255 + (d[0] * (255 - a) + 127) / 255);
  d[1] = uint8_t(c[1] * a / 255 + (d[1] * (255 - a) + 127) / 255);
  d[2] = uint8_t(c[2] * a / 255 + (d[2] * (255 - a) + 127) / 255);
  d[3] = uint8_t(a + (d[3] * (255 - a) + 127) / 255);
}

// Signed distance from a pixel centre to a rounded box (negative inside).
inline float roundBox(float px, float py, float x0, float y0, float x1, float y1, float r) {
  const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  const float qx = std::fabs(px - cx) - ((x1 - x0) / 2 - r);
  const float qy = std::fabs(py - cy) - ((y1 - y0) / 2 - r);
  const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}

struct GlassEdge {
  int top, side, bottom;
};
} // namespace

// What the helpers below draw into: premultiplied RGBA rows.
struct Canvas {
  uint8_t *surface;
  int surfaceW, surfaceH;
};

namespace {
inline float segmentDistance(float px, float py, const std::array<float, 2> &a, const std::array<float, 2> &b) {
  const float dx = b[0] - a[0], dy = b[1] - a[1];
  const float len = dx * dx + dy * dy;
  float t = len > 0 ? ((px - a[0]) * dx + (py - a[1]) * dy) / len : 0;
  t = std::max(0.f, std::min(1.f, t));
  const float ex = a[0] + t * dx - px, ey = a[1] + t * dy - py;
  return std::sqrt(ex * ex + ey * ey);
}
} // namespace

void glassFill(Canvas &cv, float x0, float y0, float x1, float y1, float r, const std::array<int, 3> &c, int alpha) {
  const int top = std::max(0, int(y0)), bottom = std::min(cv.surfaceH, int(std::ceil(y1)));
  const int left = std::max(0, int(x0)), right = std::min(cv.surfaceW, int(std::ceil(x1)));
  for (int row = top; row < bottom; ++row) {
    const float py = row + 0.5f;
    // Rows clear of the corners only need their two ends worked out.
    const bool plain = py - y0 >= r + 1 && y1 - py >= r + 1;
    uint8_t *d = &cv.surface[(size_t(row) * cv.surfaceW + left) * 4];
    for (int col = left; col < right; ++col, d += 4) {
      const float px = col + 0.5f;
      float cover = 1;
      if (!plain || px - x0 < 1 || x1 - px < 1 || py - y0 < 1 || y1 - py < 1)
        cover = std::max(0.f, std::min(1.f, 0.5f - roundBox(px, py, x0, y0, x1, y1, r)));
      blend(d, c, int(alpha * cover + 0.5f));
    }
  }
}

// The rim: a thin line along the inside of the box, faint on the sides,
// brightest along the top and again a little at the bottom, like light
// caught by the edge of a pane.
void glassEdge(Canvas &cv, float x0, float y0, float x1, float y1, float r, float width, GlassEdge e) {
  const int top = std::max(0, int(y0)), bottom = std::min(cv.surfaceH, int(std::ceil(y1)));
  const int left = std::max(0, int(x0)), right = std::min(cv.surfaceW, int(std::ceil(x1)));
  const float band = width + 1.5f;
  for (int row = top; row < bottom; ++row) {
    const float py = row + 0.5f;
    const float t = (py - y0) / (y1 - y0);
    float glow = t < 0.18f ? 1 : t < 0.43f ? 1 - (t - 0.18f) / 0.25f : 0;
    if (t > 0.8f)
      glow = float(e.bottom) / e.top * (t - 0.8f) / 0.2f;
    const float a = e.side + e.top * glow * (1 - e.side / 255.f);
    const bool nearEdge = py - y0 < r + band || y1 - py < r + band;
    for (int col = left; col < right; ++col) {
      const float px = col + 0.5f;
      if (!nearEdge && px - x0 > band && x1 - px > band) {
        col = std::max(col, int(x1 - band) - 1);
        continue;
      }
      const float dist = roundBox(px, py, x0, y0, x1, y1, r);
      const float cover = std::max(0.f, std::min(1.f, std::min(0.5f - dist, dist + width + 0.5f)));
      if (cover > 0)
        blend(&cv.surface[(size_t(row) * cv.surfaceW + col) * 4], {{255, 255, 255}}, int(a * cover + 0.5f));
    }
  }
}

void strokeLines(Canvas &cv, const std::vector<std::array<float, 2>> &pts, float width, int alpha) {
  float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
  for (const auto &p : pts) {
    minX = std::min(minX, p[0]), minY = std::min(minY, p[1]);
    maxX = std::max(maxX, p[0]), maxY = std::max(maxY, p[1]);
  }
  const float grow = width / 2 + 1;
  for (int row = std::max(0, int(minY - grow)); row < std::min(cv.surfaceH, int(maxY + grow) + 1); ++row)
    for (int col = std::max(0, int(minX - grow)); col < std::min(cv.surfaceW, int(maxX + grow) + 1); ++col) {
      float dist = 1e9f;
      for (size_t i = 1; i < pts.size(); ++i)
        dist = std::min(dist, segmentDistance(col + 0.5f, row + 0.5f, pts[i - 1], pts[i]));
      const float cover = std::max(0.f, std::min(1.f, width / 2 + 0.5f - dist));
      if (cover > 0)
        blend(&cv.surface[(size_t(row) * cv.surfaceW + col) * 4], ink, int(alpha * cover + 0.5f));
    }
}

// A filled outline, 4x4 samples a pixel (the shapes are small icons).
void fillShape(Canvas &cv, const std::vector<std::array<float, 2>> &pts, int alpha) {
  float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
  for (const auto &p : pts) {
    minX = std::min(minX, p[0]), minY = std::min(minY, p[1]);
    maxX = std::max(maxX, p[0]), maxY = std::max(maxY, p[1]);
  }
  for (int row = std::max(0, int(minY)); row < std::min(cv.surfaceH, int(maxY) + 1); ++row)
    for (int col = std::max(0, int(minX)); col < std::min(cv.surfaceW, int(maxX) + 1); ++col) {
      int inside = 0;
      for (int sy = 0; sy < 4; ++sy)
        for (int sx = 0; sx < 4; ++sx) {
          const float x = col + (sx + 0.5f) / 4, y = row + (sy + 0.5f) / 4;
          bool in = false;
          for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
            if ((pts[i][1] > y) != (pts[j][1] > y) &&
                x < (pts[j][0] - pts[i][0]) * (y - pts[i][1]) / (pts[j][1] - pts[i][1]) + pts[i][0])
              in = !in;
          inside += in;
        }
      if (inside)
        blend(&cv.surface[(size_t(row) * cv.surfaceW + col) * 4], ink, alpha * inside / 16);
    }
}

void View::centredLabel(float cx, float cy, const std::string &text, int size) {
  FT_Set_Pixel_Sizes(face, 0, size);
  const int ascender = face->size->metrics.ascender >> 6;
  const int capital = glyph(size, GlyphRef{0, FT_Get_Char_Index(face, 'H')}).top;
  const int y = int(std::lround(cy + capital / 2.f)) - ascender;
  label(int(std::lround(cx)), y + 1, text, size, {0, 0, 0}, 110, false, 0);
  label(int(std::lround(cx)), y, text, size, ink, inkAlpha, false, 0);
}

void keyIcon(Canvas &cv, const std::string &name, float cx, float cy, float s, bool filled) {
  using P = std::vector<std::array<float, 2>>;
  auto at = [&](float x, float y) { return std::array<float, 2>{{cx + x * s, cy + y * s}}; };
  const float line = std::max(1.6f, 1.8f * s);
  if (name == "backspace") {
    strokeLines(cv, {at(-12, 0), at(-5, -8), at(12, -8), at(12, 8), at(-5, 8), at(-12, 0)}, line, inkAlpha);
    strokeLines(cv, {at(-1, -4), at(7, 4)}, line, inkAlpha);
    strokeLines(cv, {at(-1, 4), at(7, -4)}, line, inkAlpha);
  } else if (name == "shift") {
    P arrow{at(0, -11), at(10, 0), at(5, 0), at(5, 9), at(-5, 9), at(-5, 0), at(-10, 0), at(0, -11)};
    if (filled)
      fillShape(cv, arrow, inkAlpha);
    else
      strokeLines(cv, arrow, line, inkAlpha);
  } else if (name == "caps") {
    fillShape(cv, {at(0, -13), at(10, -2), at(5, -2), at(5, 6), at(-5, 6), at(-5, -2), at(-10, -2)}, inkAlpha);
    glassFill(cv, cx - 5 * s, cy + 9 * s, cx + 5 * s, cy + 12 * s, 1, ink, inkAlpha);
  } else if (name == "left" || name == "right" || name == "up" || name == "down") {
    // An arrow pointing up, turned to face its way.
    const float turn = name == "up" ? 0 : name == "right" ? 1 : name == "down" ? 2 : 3;
    auto rot = [&](float x, float y) {
      for (int i = 0; i < int(turn); ++i) {
        const float t = x;
        x = -y;
        y = t;
      }
      return at(x, y);
    };
    strokeLines(cv, {rot(0, 9), rot(0, -9)}, line, inkAlpha);
    strokeLines(cv, {rot(-7, -2), rot(0, -9), rot(7, -2)}, line, inkAlpha);
  } else if (name == "menu") {
    for (int k : {-10, 0, 10})
      glassFill(cv, cx - 18 * s, cy + (k - 2.2f) * s, cx + 18 * s, cy + (k + 2.2f) * s, 2.2f * s, ink, inkAlpha);
  } else if (name == "keyboard") {
    const float w = 2.4f * s;
    glassFill(cv, cx - 22 * s, cy - 15 * s, cx + 22 * s, cy + 15 * s, 5 * s, ink, inkAlpha);
    // Cut the inside back out, leaving the outline.
    const int x0 = std::max(0, int(cx - 22 * s + w)), x1 = std::min(cv.surfaceW, int(std::ceil(cx + 22 * s - w)));
    const int y0 = std::max(0, int(cy - 15 * s + w)), y1 = std::min(cv.surfaceH, int(std::ceil(cy + 15 * s - w)));
    for (int row = y0; row < y1; ++row)
      for (int col = x0; col < x1; ++col) {
        const float d = roundBox(col + 0.5f, row + 0.5f, cx - 22 * s + w, cy - 15 * s + w, cx + 22 * s - w,
                                 cy + 15 * s - w, std::max(1.f, 5 * s - w));
        const float cover = std::max(0.f, std::min(1.f, 0.5f - d));
        uint8_t *p = &cv.surface[(size_t(row) * cv.surfaceW + col) * 4];
        for (int k = 0; k < 4; ++k)
          p[k] = uint8_t(p[k] * (1 - cover));
      }
    for (float row : {-7.5f, -1.5f})
      for (int i = 0; i < 7; ++i) {
        const float x = cx + (-15 + i * 5) * s;
        glassFill(cv, x - 1.7f * s, cy + (row - 1.7f) * s, x + 1.7f * s, cy + (row + 1.7f) * s, 0.6f * s, ink, inkAlpha);
      }
    glassFill(cv, cx - 10 * s, cy + 5.5f * s, cx + 10 * s, cy + 8.5f * s, 1.5f * s, ink, inkAlpha);
  }
}

const Image &View::keyboardPanel(const GameKeyboard &keyboard) {
  const auto page = keyboard.currentPage();
  std::string key = std::to_string(int(page)) + (keyboard.isSplit() ? "s" : "d") +
                    (keyboard.capsLock() ? "C" : keyboard.shifted() ? "S" : "-") + (keyboard.ctrlHeld() ? "c" : "-") +
                    (keyboard.altHeld() ? "a" : "-");
  for (auto it = keyboardImages.begin(); it != keyboardImages.end(); ++it)
    if (it->first == key) {
      keyboardImages.splice(keyboardImages.begin(), keyboardImages, it);
      return keyboardImages.front().second;
    }
  // A few kept, so Shift on and off (or a page and back) is drawn once.
  if (keyboardImages.size() >= 3)
    keyboardImages.pop_back();
  keyboardImages.emplace_front(key, Image::blank(944, GameKeyboard::bottom - GameKeyboard::top));
  Image &image = keyboardImages.front().second;
  surface = image.rgba.data();
  surfaceW = image.width;
  surfaceH = image.height;
  Canvas cv{surface, surfaceW, surfaceH};
  const float ox = 8, oy = GameKeyboard::top;

  const auto &layout = keyboard.layout();
  for (const auto &p : layout.panels) {
    const float x0 = p.x - ox, y0 = p.y - oy, x1 = x0 + p.w, y1 = y0 + p.h;
    glassFill(cv, x0, y0, x1, y1, 16, {205, 212, 224}, 30);
    glassEdge(cv, x0, y0, x1, y1, 16, 1.4f, {150, 70, 110});
  }
  for (const auto &k : layout.keys) {
    const float x0 = k.rect.x - ox, y0 = k.rect.y - oy, x1 = x0 + k.rect.w, y1 = y0 + k.rect.h;
    const bool lit = (k.code == SDL_SCANCODE_LSHIFT && keyboard.shifted()) ||
                     (k.code == SDL_SCANCODE_LCTRL && keyboard.ctrlHeld()) ||
                     (k.code == SDL_SCANCODE_LALT && keyboard.altHeld()) ||
                     (k.code == GameKeyboard::Functions && page == GameKeyboard::FunctionPage);
    glassFill(cv, x0, y0, x1, y1, 8, {255, 255, 255}, lit ? 58 : 13);
    glassFill(cv, x0 + 2, y0 + 2, x1 - 2, y0 + (y1 - y0) * 0.42f, 6, {255, 255, 255}, 10);
    glassEdge(cv, x0, y0, x1, y1, 8, 1.2f, lit ? GlassEdge{175, 110, 125} : GlassEdge{150, 78, 125});
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    if (k.kind == GameKeyboard::Icon) {
      std::string name = k.label;
      if (name == "shift" && keyboard.capsLock())
        name = "caps";
      keyIcon(cv, name, cx, cy, 1, keyboard.shifted());
    } else {
      centredLabel(cx, cy, keyboard.keyText(k), k.kind == GameKeyboard::Char ? 24 : 16);
    }
  }
  endLayer();
  image.touch();
  return image;
}

const Image &View::keyPreview(const std::string &text, int w, int h) {
  if (beginLayer(previewLayer, "preview|" + text, w, h)) {
    Canvas cv{surface, surfaceW, surfaceH};
    glassFill(cv, 0, 0, float(w), float(h), 12, {225, 230, 240}, 95);
    glassEdge(cv, 0, 0, float(w), float(h), 12, 1.6f, {200, 130, 150});
    centredLabel(w / 2.f, h / 2.f, text, 34);
    endLayer();
  }
  return previewLayer.image;
}

void drawGameButtons(Image &image, int gameX, bool keyboardOpen, bool menuLit) {
  KeyboardRect menu;
  const bool hasMenu = GameKeyboard::menuRect(gameX, menu);
  const auto kb = GameKeyboard::iconRect(gameX);
  const int w = kb.x + kb.w + 8, h = kb.y + kb.h + 8;
  if (image.width != w || image.height != h)
    image = Image::blank(w, h);
  else
    std::fill(image.rgba.begin(), image.rgba.end(), 0);
  Canvas cv{image.rgba.data(), w, h};
  // Just the icons, a little larger than inside a frame; dimmer until lit.
  auto button = [&](const KeyboardRect &r, const char *icon, bool lit) {
    keyIcon(cv, icon, r.x + r.w / 2.f, r.y + r.h / 2.f, r.w / 64.f * 1.3f, false);
    if (!lit)
      for (int row = std::max(0, r.y - 4); row < std::min(h, r.y + r.h + 4); ++row)
        for (int col = std::max(0, r.x - 4); col < std::min(w, r.x + r.w + 4); ++col) {
          uint8_t *p = &image.rgba[(size_t(row) * w + col) * 4];
          for (int k = 0; k < 4; ++k)
            p[k] = uint8_t(p[k] * 180 / 255);
        }
  };
  if (hasMenu)
    button(menu, "menu", menuLit);
  button(kb, "keyboard", keyboardOpen);
  image.touch();
}
} // namespace hardrpg
