// SPDX-License-Identifier: GPL-3.0-or-later
// HardRPG Dante 98 backend (app0:/opendt.self): runs games made with RPG
// Maker Dante 98 and Dante 98 II natively with OpenDT (MIT), instead of
// emulating a PC-98. Started by the launcher with --hardrpg-play and a request
// in selection.json; returns to the launcher with --hardrpg-return.
#include "app/games.h"
#include "core/host.h"
#include "core/timing.h"
#include "core/util.h"
#include "d1/d1.h"
#include "d2/d2.h"
#include "d2/exe_text.h"
#include "music/player.h"
#include "display/fast-forward.h"
#include "input/vita-keyboard.h"
#include "input/vita_controls.h"
#include "native_launcher/game_menu.h"
#include "native_launcher/i18n.h"
#include "native_launcher/model.h"
#include "native_launcher/opendt_handoff.h"
#include "native_launcher/pc98_fat.h"
#include "native_launcher/view.h"

#include <psp2/appmgr.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <vita2d.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

int _newlib_heap_size_user = 96 * 1024 * 1024;

namespace {
constexpr const char *kRequest = "ux0:/data/hardrpg/selection.json";
constexpr const char *kStats = "ux0:/data/hardrpg/reports/opendt-stats.txt";
constexpr const char *kAutotest = "ux0:/data/hardrpg/opendt/autotest.txt";
// Development only: a TEST package whose eboot.bin is this program boots
// straight into the game in selection.json when this file exists, and exits
// to the home screen instead of returning to a launcher.
constexpr const char *kDirect = "ux0:/data/hardrpg/opendt/direct-boot.txt";
bool direct;

void report(const std::string &message) {
  if (FILE *f = std::fopen("ux0:/data/hardrpg/last-error.txt", "wb")) {
    std::fprintf(f, "Dante 98 (OpenDT)\n%s\n", message.c_str());
    std::fclose(f);
  }
  sceClibPrintf("HardRPG OpenDT error: %s\n", message.c_str());
}

void stat_line(const char *fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  sceClibPrintf("HardRPG OpenDT: %s\n", line);
  if (FILE *f = std::fopen(kStats, "ab")) {
    std::fprintf(f, "%s\n", line);
    std::fclose(f);
  }
}

uint64_t now_us() { return sceKernelGetProcessTimeWide(); }

bool exists(const std::string &path) {
  SceIoStat st;
  return sceIoGetstat(path.c_str(), &st) >= 0;
}
void mkdirs(const std::string &path) {
  for (size_t i = path.find('/', path.find(":/") + 2); i != std::string::npos;
       i = path.find('/', i + 1))
    sceIoMkdir(path.substr(0, i).c_str(), 0777);
  sceIoMkdir(path.c_str(), 0777);
}
std::string read_text(const std::string &path) {
  std::string out;
  if (FILE *f = std::fopen(path.c_str(), "rb")) {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
      out.append(buf, n);
    std::fclose(f);
  }
  return out;
}
bool write_bytes(const std::string &path, const void *data, size_t size) {
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f)
    return false;
  bool ok = std::fwrite(data, 1, size, f) == size;
  return std::fclose(f) == 0 && ok;
}

// ---------------------------------------------------------------- settings
// The game's HardRPG settings, as for every other engine: the defaults in
// launcher-config.json under the game's own config/<key>.json. Changed live
// by the in-game menu.
struct Settings {
  bool aspect = true;  // fixedAspectRatio: a PC-98 picture is 4:3
  bool integer = false; // integerScalingActive
  int filter = 5;      // smoothScaling: 0 nearest, 1 smooth, 5 sharp
  int metrics = 0;     // performanceMetrics: 0 off .. 3 full stats
  bool fastForward = true;
  int boost = 100;     // audioBoost, percent
};

Settings settings_from(const json5pp::value &v) {
  Settings out;
  if (!v.is_object())
    return out;
  auto &o = v.as_object();
  auto boolean = [&](const char *k, bool &dst) {
    auto i = o.find(k);
    if (i != o.end() && i->second.is_boolean())
      dst = i->second.as_boolean();
  };
  auto integer = [&](const char *k, int &dst, int lo, int hi) {
    auto i = o.find(k);
    if (i != o.end() && i->second.is_integer() && i->second.as_integer() >= lo &&
        i->second.as_integer() <= hi)
      dst = int(i->second.as_integer());
  };
  boolean("fixedAspectRatio", out.aspect);
  boolean("integerScalingActive", out.integer);
  boolean("fastForward", out.fastForward);
  integer("smoothScaling", out.filter, 0, 5);
  integer("performanceMetrics", out.metrics, 0, 3);
  integer("audioBoost", out.boost, 100, 400);
  if (hardrpg::filterIndex(json5pp::value(json5pp::value::integer_type(out.filter))) < 0)
    out.filter = hardrpg::filterDefault;
  return out;
}

json5pp::value merged_settings(const std::string &key) {
  auto merged = json5pp::object({});
  for (std::string path : {std::string("ux0:/data/hardrpg/launcher-config.json"),
                           "ux0:/data/hardrpg/config/" + key + ".json"}) {
    std::string text = read_text(path);
    if (text.empty())
      continue;
    try {
      auto v = json5pp::parse5(text);
      if (v.is_object())
        for (auto &kv : v.as_object())
          merged.as_object()[kv.first] = kv.second;
    } catch (...) {
    }
  }
  return merged;
}

hardrpg::ControlProfile controls_for(const std::string &key) {
  std::string text = read_text("ux0:/data/hardrpg/controls/" + key + ".json");
  if (!text.empty())
    try {
      return hardrpg::ControlProfile::parse(json5pp::parse5(text));
    } catch (...) {
    }
  return {};
}

// ---------------------------------------------------------------- screen
vita2d_pgf *font;
void loading_screen(const std::string &title, const std::string &line) {
  vita2d_start_drawing();
  vita2d_clear_screen();
  vita2d_pgf_draw_text(font, 60, 250, RGBA8(0xf0, 0xe6, 0xd2, 0xff), 1.4f, title.c_str());
  vita2d_pgf_draw_text(font, 60, 300, RGBA8(0xa0, 0x98, 0x8c, 0xff), 1.0f, line.c_str());
  vita2d_end_drawing();
  vita2d_swap_buffers();
}

// Pictures go to the screen on their own thread, so the game never waits for
// a free screen buffer. Three slots: the game fills one, one waits as the
// newest, and the screen thread draws the third (same design as the PC-98
// emulator backend).
constexpr int kW = core::kScreenW, kH = core::kScreenH;
struct Slot {
  vita2d_texture *tex = nullptr;
};
Slot slots[3];
// Drawing is shared by the screen thread and, while the menu is open, the
// game thread.
SceUID draw_lock = -1;
struct DrawLock {
  DrawLock() { sceKernelLockMutex(draw_lock, 1, nullptr); }
  ~DrawLock() { sceKernelUnlockMutex(draw_lock, 1); }
};

// How the picture is placed, from the settings (read by the screen thread).
struct Placement {
  float x = 0, y = 0, w = 960, h = 544;
  int filter = 5;
};
std::atomic<int> placement_at{0};
Placement placements[2];
Placement placement_for(const Settings &st) {
  Placement p;
  p.filter = st.filter;
  // PC-98 monitors are 4:3: 640x400 pixels are taller than they are wide.
  float h = 544.f, w = st.aspect ? h * 4.f / 3.f : 960.f;
  if (st.integer) {
    h = float(kH); // the only whole size that fits: one screen pixel per line
    w = st.aspect ? h * 4.f / 3.f : float(kW);
  }
  p.w = w;
  p.h = h;
  p.x = std::floor((960.f - w) / 2.f);
  p.y = std::floor((544.f - h) / 2.f);
  return p;
}
void set_placement(const Settings &st) {
  int next = placement_at.load() ^ 1;
  placements[next] = placement_for(st);
  placement_at.store(next);
}
// Sharp: the picture is first doubled with whole pixels, then scaled
// smoothly, so pixel edges stay crisp without uneven columns.
vita2d_texture *sharp_target = nullptr;

// Images over the game (premultiplied RGBA from the launcher's view): the
// menu icon in the left border and the stats column. Two textures each, so
// a new picture never overwrites one the GPU may still be reading.
struct Overlay {
  vita2d_texture *tex[2] = {nullptr, nullptr};
  int w[2] = {0, 0}, h[2] = {0, 0};
  std::atomic<int> shown{-1};
  int x = 0, y = 0;
  void set(const hardrpg::Image &img, int px, int py) {
    set(img.rgba.data(), img.width, img.height, px, py);
  }
  void set(const uint8_t *rgba, int width, int height, int px, int py) {
    struct {
      int width, height;
    } img{width, height};
    int next = shown.load() == 0 ? 1 : 0;
    if (!tex[next] || w[next] < img.width || h[next] < img.height) {
      if (tex[next]) {
        vita2d_wait_rendering_done();
        vita2d_free_texture(tex[next]);
      }
      tex[next] = vita2d_create_empty_texture(std::max(img.width, 8), std::max(img.height, 8));
      w[next] = img.width;
      h[next] = img.height;
      if (!tex[next])
        return;
      vita2d_texture_set_filters(tex[next], SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
    }
    auto *dst = static_cast<uint8_t *>(vita2d_texture_get_datap(tex[next]));
    const unsigned stride = vita2d_texture_get_stride(tex[next]);
    std::memset(dst, 0, size_t(stride) * h[next]);
    // vita2d blends straight alpha: undo the premultiplication.
    for (int row = 0; row < img.height; ++row) {
      const uint8_t *src = rgba + size_t(row) * img.width * 4;
      uint8_t *out = dst + size_t(row) * stride;
      for (int col = 0; col < img.width; ++col, src += 4, out += 4) {
        unsigned a = src[3];
        if (!a) {
          out[0] = out[1] = out[2] = out[3] = 0;
          continue;
        }
        out[0] = uint8_t(std::min(255u, src[0] * 255u / a));
        out[1] = uint8_t(std::min(255u, src[1] * 255u / a));
        out[2] = uint8_t(std::min(255u, src[2] * 255u / a));
        out[3] = uint8_t(a);
      }
    }
    x = px;
    y = py;
    shown.store(next);
  }
  void hide() { shown.store(-1); }
  void draw() const {
    int i = shown.load();
    if (i >= 0 && tex[i])
      vita2d_draw_texture(tex[i], float(x), float(y));
  }
};
Overlay menu_icon, stats_panel;

// The in-game menu on the GPU, as the main engine draws it
// (GameMenuLayer::draw): the view's quads, its images as textures kept while
// their pixels stay the same, over the paused game blurred and dimmed.
// Composing the menu on the CPU instead cost a full 960x544 picture per
// change and made the menu lag.
struct MenuPainter {
  struct Tex {
    vita2d_texture *tex = nullptr;
    uint64_t id = 0;
    int radius = -1; // the rounding baked into the texture, in its pixels
    int used = 0;
  };
  std::map<const hardrpg::Image *, Tex> images;
  std::map<int, vita2d_texture *> discs; // white discs by radius, for corners
  vita2d_texture *blur_half = nullptr, *blur_quarter = nullptr;
  bool blurred = false;
  int frame = 0;

  static float corner_cover(float px, float py, float r) {
    // As the engine's shader: 1 inside, a pixel of soft edge on the arc.
    float dx = r - px, dy = r - py;
    return std::max(0.f, std::min(1.f, r + 0.5f - std::sqrt(dx * dx + dy * dy)));
  }
  // The image un-premultiplied (vita2d blends straight alpha), its corners
  // rounded by radius (in the image's pixels) as the shader rounds them.
  vita2d_texture *texture(const hardrpg::Image &img, int radius) {
    Tex &t = images[&img];
    t.used = frame;
    if (t.tex && t.id == img.id && t.radius == radius)
      return t.tex;
    if (t.tex) {
      vita2d_wait_rendering_done();
      vita2d_free_texture(t.tex);
    }
    t.tex = vita2d_create_empty_texture(img.width, img.height);
    t.id = img.id;
    t.radius = radius;
    if (!t.tex)
      return nullptr;
    vita2d_texture_set_filters(t.tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    auto *dst = static_cast<uint8_t *>(vita2d_texture_get_datap(t.tex));
    const unsigned stride = vita2d_texture_get_stride(t.tex);
    const float r = float(radius);
    for (int row = 0; row < img.height; ++row) {
      const uint8_t *src = img.rgba.data() + size_t(row) * img.width * 4;
      uint8_t *out = dst + size_t(row) * stride;
      float qy = std::min(row + 0.5f, img.height - row - 0.5f);
      for (int col = 0; col < img.width; ++col, src += 4, out += 4) {
        unsigned a = src[3];
        if (!a) {
          out[0] = out[1] = out[2] = out[3] = 0;
          continue;
        }
        out[0] = uint8_t(std::min(255u, src[0] * 255u / a));
        out[1] = uint8_t(std::min(255u, src[1] * 255u / a));
        out[2] = uint8_t(std::min(255u, src[2] * 255u / a));
        if (radius > 0) {
          float qx = std::min(col + 0.5f, img.width - col - 0.5f);
          if (qx < r && qy < r)
            a = unsigned(a * corner_cover(qx, qy, r) + 0.5f);
        }
        out[3] = uint8_t(a);
      }
    }
    return t.tex;
  }
  vita2d_texture *disc(int r) {
    auto &d = discs[r];
    if (d)
      return d;
    d = vita2d_create_empty_texture(2 * r, 2 * r);
    if (!d)
      return nullptr;
    vita2d_texture_set_filters(d, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
    auto *dst = static_cast<uint32_t *>(vita2d_texture_get_datap(d));
    const unsigned stride = vita2d_texture_get_stride(d) / 4;
    for (int y = 0; y < 2 * r; ++y)
      for (int x = 0; x < 2 * r; ++x) {
        float qx = std::min(x + 0.5f, 2 * r - x - 0.5f), qy = std::min(y + 0.5f, 2 * r - y - 0.5f);
        unsigned a = unsigned(255 * corner_cover(qx, qy, float(r)) + 0.5f);
        dst[y * stride + x] = (a << 24) | 0xffffffu;
      }
    return d;
  }
  // A solid rectangle with rounded corners, in pieces that never overlap
  // (the colour is see-through more often than not).
  void rounded(float x, float y, float w, float h, float radius, unsigned color) {
    int r = int(std::min(radius, std::min(w, h) / 2) + 0.5f);
    vita2d_texture *d = r > 0 ? disc(r) : nullptr;
    if (!d) {
      vita2d_draw_rectangle(x, y, w, h, color);
      return;
    }
    const float R = float(r);
    vita2d_draw_texture_tint_part_scale(d, x, y, 0, 0, R, R, 1, 1, color);
    vita2d_draw_texture_tint_part_scale(d, x + w - R, y, R, 0, R, R, 1, 1, color);
    vita2d_draw_texture_tint_part_scale(d, x, y + h - R, 0, R, R, R, 1, 1, color);
    vita2d_draw_texture_tint_part_scale(d, x + w - R, y + h - R, R, R, R, R, 1, 1, color);
    if (w > 2 * R) {
      vita2d_draw_rectangle(x + R, y, w - 2 * R, R, color);
      vita2d_draw_rectangle(x + R, y + h - R, w - 2 * R, R, color);
    }
    if (h > 2 * R)
      vita2d_draw_rectangle(x, y + R, w, h - 2 * R, color);
  }
  static unsigned rgba(float r, float g, float b, float a) {
    auto c = [](float v) { return unsigned(std::max(0.f, std::min(1.f, v)) * 255 + 0.5f); };
    return RGBA8(c(r), c(g), c(b), c(a));
  }
  // The paused game, blurred: drawn at half then a quarter of the screen's
  // size (as the engine's glass passes), once, as the game stands still.
  void blur(void (*draw_game_into)(vita2d_texture *)) {
    if (!blur_half) {
      blur_half = vita2d_create_empty_texture_rendertarget(480, 272, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
      blur_quarter = vita2d_create_empty_texture_rendertarget(240, 136, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
      for (auto *t : {blur_half, blur_quarter})
        if (t)
          vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    }
    if (!blur_half || !blur_quarter)
      return;
    draw_game_into(blur_half);
    // A render target keeps the screen's 960x544 coordinates.
    vita2d_start_drawing_advanced(blur_quarter, 0);
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
    vita2d_clear_screen();
    vita2d_draw_texture_scale(blur_half, 0, 0, 2, 2);
    vita2d_end_drawing();
    blurred = true;
  }
  void draw(const hardrpg::View &view) {
    ++frame;
    if (blurred) {
      vita2d_draw_texture_scale(blur_quarter, 0, 0, 4, 4);
      vita2d_draw_rectangle(0, 0, 960, 544, rgba(0.03f, 0.03f, 0.04f, 0.5f));
    } else
      vita2d_draw_rectangle(0, 0, 960, 544, rgba(0.03f, 0.03f, 0.04f, 0.64f));
    bool clipping = false;
    for (auto &q : view.scene) {
      if (q.w <= 0 || q.h <= 0 || q.alpha <= 0.002f || (q.image && q.image->empty()))
        continue;
      bool clip = q.clipTop > 0 || q.clipBottom < 544 || q.clipLeft > 0 || q.clipRight < 960;
      if (clip != clipping) {
        if (clip)
          vita2d_enable_clipping();
        else
          vita2d_disable_clipping();
        clipping = clip;
      }
      if (clip)
        vita2d_set_clip_rectangle(q.clipLeft, q.clipTop, q.clipRight, q.clipBottom);
      if (q.image) {
        const hardrpg::Image &img = *q.image;
        int radius = int(q.radius * img.width / q.w + 0.5f);
        if (vita2d_texture *t = texture(img, radius))
          vita2d_draw_texture_tint_scale(t, q.x, q.y, q.w / img.width, q.h / img.height,
                                         rgba(q.bright, q.bright, q.bright, q.alpha));
      } else {
        rounded(q.x, q.y, q.w, q.h, q.radius,
                rgba(q.color[0] / 255.f, q.color[1] / 255.f, q.color[2] / 255.f, q.alpha));
      }
    }
    if (clipping)
      vita2d_disable_clipping();
    // Images the view no longer shows (labels of rows left behind).
    if (frame % 60 == 0)
      for (auto it = images.begin(); it != images.end();)
        if (frame - it->second.used > 120) {
          vita2d_wait_rendering_done();
          vita2d_free_texture(it->second.tex);
          it = images.erase(it);
        } else
          ++it;
  }
  void release() {
    vita2d_wait_rendering_done();
    for (auto &i : images)
      if (i.second.tex)
        vita2d_free_texture(i.second.tex);
    images.clear();
    for (auto &d : discs)
      if (d.second)
        vita2d_free_texture(d.second);
    discs.clear();
    for (auto **t : {&blur_half, &blur_quarter})
      if (*t) {
        vita2d_free_texture(*t);
        *t = nullptr;
      }
    blurred = false;
  }
};
constexpr int kFresh = 4;
std::atomic<int> middle{1};
int fill_slot = 0, shown_slot = 2;
std::atomic<bool> screen_run{false};
std::atomic<uint32_t> screen_draws{0};
SceUID screen_sema = -1;

// Draws the game picture (no scene begun yet: Sharp needs a scene of its own).
void draw_game(const Slot &s, bool begin_screen) {
  const Placement &p = placements[placement_at.load()];
  bool sharp = p.filter == 5 && sharp_target && !(p.w == kW && p.h == kH);
  if (sharp) {
    vita2d_texture_set_filters(s.tex, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
    vita2d_start_drawing_advanced(sharp_target, 0);
    // vita2d keeps the screen's 960x544 coordinates on a render target:
    // filling them fills the doubled picture.
    vita2d_draw_texture_scale(s.tex, 0, 0, 960.f / kW, 544.f / kH);
    vita2d_end_drawing();
  }
  if (begin_screen) {
    vita2d_start_drawing();
    vita2d_clear_screen();
  }
  if (sharp) {
    vita2d_draw_texture_scale(sharp_target, p.x, p.y, p.w / (2 * kW), p.h / (2 * kH));
  } else {
    auto f = p.filter == 0 ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR;
    vita2d_texture_set_filters(s.tex, f, f);
    vita2d_draw_texture_scale(s.tex, p.x, p.y, p.w / kW, p.h / kH);
  }
}

// The game at its place on the screen, into a render target (the menu's
// blur), with smooth filtering whatever the game's own filter.
void draw_game_into(vita2d_texture *target);

// Fast forward on: the main engine's badge (FastForwardBadge), two white
// chevrons with a black outline in the top right corner.
std::atomic<bool> ff_badge{false};
void draw_ff_badge() {
  const int left = 960 - 50, top = 10, half = 6, cell = 2, thick = 3;
  for (int grow : {2, 0}) {
    unsigned color = grow ? RGBA8(0, 0, 0, 255) : RGBA8(255, 255, 255, 255);
    for (int c = 0; c < 2; ++c)
      for (int row = 0; row <= half * 2; ++row) {
        int step = half - (row > half ? row - half : half - row);
        vita2d_draw_rectangle(float(left + c * 16 + step * cell - grow), float(top + row * cell - grow),
                              float(thick * cell + grow * 2), float(cell + grow * 2), color);
      }
  }
}

void draw_slot(const Slot &s) {
  DrawLock lock;
  draw_game(s, true);
  menu_icon.draw();
  stats_panel.draw();
  if (ff_badge.load())
    draw_ff_badge();
  vita2d_end_drawing();
  vita2d_swap_buffers();
}

int screen_thread(SceSize, void *) {
  unsigned last_vcount = sceDisplayGetVcount() - 1;
  while (screen_run.load()) {
    SceUInt timeout = 100000;
    sceKernelWaitSema(screen_sema, 1, &timeout);
    if (!(middle.load() & kFresh))
      continue;
    shown_slot = middle.exchange(shown_slot) & 3;
    if (sceDisplayGetVcount() == last_vcount)
      sceDisplayWaitVblankStart();
    draw_slot(slots[shown_slot]);
    last_vcount = sceDisplayGetVcount();
    screen_draws.fetch_add(1);
  }
  vita2d_wait_rendering_done();
  return 0;
}

void create_slots() {
  for (auto &s : slots) {
    // The games draw with 16 colors: the picture goes to the GPU as color
    // indices plus a palette, which is about ten times cheaper to prepare on
    // the Vita's CPU than full-color pixels.
    s.tex = vita2d_create_empty_texture_format(kW, kH, SCE_GXM_TEXTURE_FORMAT_P8_ABGR);
    if (!s.tex)
      throw std::runtime_error("Not enough video memory for the game screen");
  }
  // Doubled picture for the Sharp filter.
  sharp_target = vita2d_create_empty_texture_rendertarget(2 * kW, 2 * kH,
                                                          SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
  if (sharp_target)
    vita2d_texture_set_filters(sharp_target, SCE_GXM_TEXTURE_FILTER_LINEAR,
                               SCE_GXM_TEXTURE_FILTER_LINEAR);
}

// Draws the game's screen into a free slot and hands it to the screen thread.
void present(const core::Screen &scr) {
  // The picture is put together in ordinary memory, then copied to the
  // texture in one pass: texture memory is uncached, and reading it back
  // (composing in place) took 11 ms a frame on the Vita.
  static uint8_t indexed[kW * kH];
  Slot &s = slots[fill_slot];
  scr.composeIndexed(indexed, kW);
  auto *dst = static_cast<uint8_t *>(vita2d_texture_get_datap(s.tex));
  const unsigned stride = vita2d_texture_get_stride(s.tex);
  if (stride == unsigned(kW))
    std::memcpy(dst, indexed, sizeof indexed);
  else
    for (int y = 0; y < kH; ++y)
      std::memcpy(dst + size_t(y) * stride, indexed + size_t(y) * kW, kW);
  scr.paletteAbgr(static_cast<uint32_t *>(vita2d_texture_get_palette(s.tex)));
  fill_slot = middle.exchange(fill_slot | kFresh) & 3;
  sceKernelSignalSema(screen_sema, 1);
}

// ---------------------------------------------------------------- sound
// The music player makes its own samples; the sound thread just asks it for
// the next block and hands that to the system.
constexpr int kRate = 44100, kGrain = 512;
std::atomic<music::MusicPlayer *> player{nullptr};
std::atomic<bool> audio_run{true};
// The menu pauses the music; the volume boost (percent) applies live.
std::atomic<bool> audio_paused{false};
std::atomic<int> audio_boost{100};
int audio_port = -1;

int audio_thread(SceSize, void *) {
  static int16_t out[kGrain * 2];
  while (audio_run.load()) {
    auto *p = player.load();
    if (p && !audio_paused.load()) {
      p->generate(out, kGrain);
      int boost = audio_boost.load();
      if (boost != 100)
        for (auto &v : out)
          v = int16_t(std::max(-32768, std::min(32767, int(v) * boost / 100)));
    } else {
      std::memset(out, 0, sizeof out);
    }
    if (sceAudioOutOutput(audio_port, out) < 0)
      sceKernelDelayThread(kGrain * 1000000 / kRate);
  }
  return 0;
}

// ---------------------------------------------------------------- input
// The same chords as HardRPG's other engines: L+R+Start held opens the menu,
// L+R+Select held returns to the library, a tap of L+R switches fast forward.
constexpr uint32_t kReturnChord = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT;
constexpr uint32_t kMenuChord = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_START;
constexpr uint64_t kHoldUs = 500000;

// Pad bits in hardrpg::controlSources order.
uint16_t pad_bits(uint32_t sce) {
  static const uint32_t order[] = {SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE, SCE_CTRL_TRIANGLE,
                                   SCE_CTRL_SELECT, SCE_CTRL_START, SCE_CTRL_UP, SCE_CTRL_DOWN,
                                   SCE_CTRL_LEFT, SCE_CTRL_RIGHT, SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER};
  uint16_t bits = 0;
  for (int i = 0; i < 12; ++i)
    if (sce & order[i])
      bits |= uint16_t(1u << i);
  return bits;
}

void add_touch(hardrpg::ControlSample &sample, bool front, bool rear) {
  static bool ready[2] = {false, false};
  static SceTouchPanelInfo info[2]{};
  for (int port = 0; port < 2; ++port) {
    if (!(port == 0 ? front : rear))
      continue;
    if (!ready[port]) {
      if (sceTouchGetPanelInfo(port, &info[port]) < 0)
        continue;
      sceTouchSetSamplingState(port, SCE_TOUCH_SAMPLING_STATE_START);
      ready[port] = true;
    }
    SceTouchData data{};
    if (sceTouchPeek(port, &data, 1) <= 0)
      continue;
    auto &panel = info[port];
    int x0 = port == 0 ? panel.minDispX : panel.minAaX;
    int y0 = port == 0 ? panel.minDispY : panel.minAaY;
    int w = (port == 0 ? panel.maxDispX : panel.maxAaX) - x0;
    int h = (port == 0 ? panel.maxDispY : panel.maxAaY) - y0;
    if (w <= 0 || h <= 0)
      continue;
    auto &contacts = port == 0 ? sample.front : sample.rear;
    for (unsigned i = 0; i < data.reportNum && i < SCE_TOUCH_MAX_REPORT; ++i)
      contacts.push_back({data.report[i].id, float(data.report[i].x - x0) / w,
                          float(data.report[i].y - y0) / h});
  }
}

hardrpg::ControlSample sample_from(const SceCtrlData &pad, bool front, bool rear) {
  hardrpg::ControlSample sample;
  sample.milliseconds = sceKernelGetProcessTimeWide() / 1000;
  sample.buttons = pad_bits(pad.buttons);
  sample.lx = pad.lx;
  sample.ly = pad.ly;
  sample.rx = pad.rx;
  sample.ry = pad.ry;
  add_touch(sample, front, rear);
  return sample;
}
} // namespace

// The launcher's controls tester (in the menu) reads the controls with this,
// as it does inside HardRPG's main engine.
hardrpg::ControlSample vitaReadControls(bool front, bool rear, bool) {
  SceCtrlData pad{};
  sceCtrlPeekBufferPositive(0, &pad, 1);
  return sample_from(pad, front, rear);
}

namespace {
// Test script (development only): ux0:/data/hardrpg/opendt/autotest.txt
// drives the game from inside the program, for runs in Vita3K. Lines:
//   <frame> press <buttons> <frames>   hold buttons (cross+up...) for N frames
//   <frame> shot <name>                save the screen to reports/opendt-<name>.png
//   <frame> quit                       return to the launcher
struct Step {
  long frame;
  std::string what, arg;
  long frames = 0;
};
std::vector<Step> script;
size_t script_at = 0;
uint32_t script_buttons = 0;
long script_until = -1;

uint32_t button_named(const std::string &name) {
  static const std::pair<const char *, uint32_t> names[] = {
      {"up", SCE_CTRL_UP},         {"down", SCE_CTRL_DOWN},     {"left", SCE_CTRL_LEFT},
      {"right", SCE_CTRL_RIGHT},   {"cross", SCE_CTRL_CROSS},   {"circle", SCE_CTRL_CIRCLE},
      {"square", SCE_CTRL_SQUARE}, {"triangle", SCE_CTRL_TRIANGLE},
      {"start", SCE_CTRL_START},   {"select", SCE_CTRL_SELECT}, {"l", SCE_CTRL_LTRIGGER},
      {"r", SCE_CTRL_RTRIGGER}};
  uint32_t b = 0;
  size_t start = 0;
  while (start <= name.size()) {
    size_t plus = name.find('+', start);
    std::string part = name.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
    for (auto &n : names)
      if (part == n.first)
        b |= n.second;
    if (plus == std::string::npos)
      break;
    start = plus + 1;
  }
  return b;
}

void load_script() {
  std::string text = read_text(kAutotest);
  size_t start = 0;
  while (start < text.size()) {
    size_t nl = text.find('\n', start);
    std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    start = nl == std::string::npos ? text.size() : nl + 1;
    char what[32] = "", arg[96] = "";
    long frame = 0, frames = 0;
    int n = std::sscanf(line.c_str(), "%ld %31s %95s %ld", &frame, what, arg, &frames);
    if (n >= 2)
      script.push_back({frame, what, n >= 3 ? arg : "", frames});
  }
  if (!script.empty())
    stat_line("test script: %u steps", (unsigned)script.size());
}

// ---------------------------------------------------------------- host
std::string game_key; // the game's 16-hex key: config/<key>.json, controls/<key>.json

class VitaHost : public core::Host {
public:
  std::vector<uint8_t> rgb = std::vector<uint8_t>(size_t(kW) * kH * 3);
  core::InputState in;
  Settings st;
  hardrpg::ControlProfile profile;
  hardrpg::ControlState controls;
  hardrpg::FastForward ff;
  bool quit = false, wait_release = false;
  long frame = 0;
  uint64_t menu_since = 0, return_since = 0;
  // A finger that went down on the menu icon opens the menu when lifted.
  bool icon_touch = false, touching = false;
  uint64_t next = 0, window0 = 0, compose_us = 0, game_us = 0, last_return = 0;
  long window_frames = 0, late_frames = 0, shown_frames = 0;
  std::unique_ptr<hardrpg::View> stats_view;
  bool stats_failed = false;

  void apply(const Settings &next_settings) {
    st = next_settings;
    ff.allowed = st.fastForward;
    audio_boost.store(st.boost);
    set_placement(st);
    place_icon(false);
    if (!st.metrics)
      stats_panel.hide();
  }
  int game_x() const { return int(placement_for(st).x); }
  void place_icon(bool lit) {
    int gx = game_x();
    hardrpg::KeyboardRect r;
    if (!hardrpg::GameKeyboard::menuRect(gx, r)) {
      menu_icon.hide();
      return;
    }
    hardrpg::Image img;
    hardrpg::drawGameButtons(img, gx, false, lit);
    // Only the menu: a Dante 98 game never asks for typed text.
    int cut = r.y + r.h + 6;
    hardrpg::Image top = hardrpg::Image::blank(img.width, std::min(img.height, cut));
    std::copy_n(img.rgba.begin(), size_t(top.width) * top.height * 4, top.rgba.begin());
    menu_icon.set(top, 0, 0);
  }

  void poll() {
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    // The test script adds its buttons to the real ones.
    while (script_at < script.size() && script[script_at].frame <= frame) {
      const Step &s = script[script_at++];
      if (s.what == "press") {
        script_buttons = button_named(s.arg);
        script_until = frame + std::max(1L, s.frames);
      } else if (s.what == "shot") {
        std::string path = "ux0:/data/hardrpg/reports/opendt-" + s.arg + ".png";
        core::writePng(path, rgb.data(), kW, kH);
        stat_line("frame %ld: saved %s", frame, path.c_str());
      } else if (s.what == "quit") {
        quit = true;
      }
    }
    if (frame >= script_until)
      script_buttons = 0;
    pad.buttons |= script_buttons;
    uint32_t b = pad.buttons;
    uint64_t now = now_us();
    auto held = [&](uint32_t chord, uint64_t &since) {
      if ((b & chord) != chord) {
        since = 0;
        return false;
      }
      if (!since)
        since = now;
      return now - since >= kHoldUs;
    };
    if (held(kReturnChord, return_since))
      quit = true;
    if (held(kMenuChord, menu_since)) {
      menu_since = ~0ull >> 1; // once per hold
      menu_wanted = true;
    }
    auto sample = sample_from(pad, true, profile.enabled);
    // The menu icon in the left border.
    bool down = false;
    float tx = 0, ty = 0;
    if (!sample.front.empty()) {
      down = true;
      tx = sample.front[0].x * 960.f;
      ty = sample.front[0].y * 544.f;
    }
    if (down && !touching)
      icon_touch = hardrpg::GameKeyboard::menuTouched(game_x(), int(tx), int(ty));
    if (!down && touching && icon_touch) {
      icon_touch = false;
      menu_wanted = true;
    }
    touching = down;

    bool l = b & SCE_CTRL_LTRIGGER, r = b & SCE_CTRL_RTRIGGER;
    core::InputState s;
    bool mapped_ff = false;
    if (wait_release) {
      // After the menu: buttons still held from it do nothing in the game.
      wait_release = b != 0;
    } else if (profile.enabled) {
      auto res = controls.update(profile, sample);
      auto &a = res.actions;
      auto &k = res.keys;
      // RPG Maker actions as the XP/VX games read them (C confirms, B and X
      // cancel or open the menu, A is Shift: the games' "fast" key), and
      // mapped keyboard keys as OpenDT reads a PC keyboard (USB scancodes).
      s.up = a[8] || k[82] || k[96];
      s.down = a[2] || k[81] || k[90];
      s.left = a[4] || k[80] || k[92];
      s.right = a[6] || k[79] || k[94];
      s.ok = a[13] || k[29] || k[40] || k[88] || k[44];
      s.cancel = a[12] || a[14] || k[27] || k[41] || k[42];
      s.fast = a[11] || k[20] || k[225] || k[229];
      s.rollUp = k[75];
      s.rollDown = k[78];
      mapped_ff = a[hardrpg::controlFastForward] || k[43];
    } else if (!(l && r)) {
      s.up = b & SCE_CTRL_UP;
      s.down = b & SCE_CTRL_DOWN;
      s.left = b & SCE_CTRL_LEFT;
      s.right = b & SCE_CTRL_RIGHT;
      // Left stick works as the d-pad.
      s.up = s.up || pad.ly < 64;
      s.down = s.down || pad.ly > 192;
      s.left = s.left || pad.lx < 64;
      s.right = s.right || pad.lx > 192;
      s.ok = b & SCE_CTRL_CROSS;
      s.cancel = b & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE | SCE_CTRL_START);
      s.fast = b & SCE_CTRL_SQUARE; // the games' "fast" key (SHIFT on a PC-98)
    }
    if (ff.update(l, r, (b & SCE_CTRL_SELECT) || (b & SCE_CTRL_START), mapped_ff))
      next = 0;
    ff_badge.store(ff.active);
    in = s;
  }

  bool menu_wanted = false;
  void run_menu();

  void stats(double fps, double frame_ms, double draw_ms) {
    if (!st.metrics)
      return;
    if (!stats_view && !stats_failed) {
      try {
        hardrpg::useLauncherLanguage();
        stats_view.reset(new hardrpg::View("app0:/font.ttf"));
      } catch (...) {
        stats_failed = true;
      }
    }
    if (!stats_view)
      return;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0f FPS", fps);
    // Dante 98 games run at the PC-98's 56.4 frames a second.
    int tone = fps >= 53 ? 1 : fps >= 30 ? 2 : 3;
    std::vector<hardrpg::View::StatRow> rows;
    if (st.metrics >= 2) {
      std::snprintf(buf + 32, 32, "%.1f / %.1f ms", frame_ms, draw_ms);
      rows.push_back({"", buf + 32, 0});
    }
    if (st.metrics >= 3) {
      char cpu[48];
      std::snprintf(cpu, sizeof cpu, "%d MHz", scePowerGetArmClockFrequency());
      rows.push_back({hardrpg::tr("CPU"), cpu, 0});
      int battery = scePowerGetBatteryLifePercent();
      if (battery >= 0) {
        char bat[48];
        std::snprintf(bat, sizeof bat, "%d%%", battery);
        std::string value = bat;
        if (scePowerIsBatteryCharging())
          value += "\n" + std::string(hardrpg::tr("charging"));
        rows.push_back({hardrpg::tr("Battery"), value, battery <= 10 ? 3 : battery <= 20 ? 2 : 0});
      }
    }
    // One narrow column in the left border, under the menu icon, as plain
    // text when it fits; otherwise on a dark panel over the game's corner.
    const int width = 104;
    int gx = game_x();
    bool in_border = gx >= width + 8;
    const hardrpg::Image &panel = stats_view->statsPanel(buf, tone, rows, width, !in_border);
    stats_panel.set(panel, in_border ? (gx - width) / 2 : 8, 544 - panel.height - 8);
  }

  void vsync(const core::Screen &scr) override {
    uint64_t t0 = now_us();
    if (last_return)
      game_us += t0 - last_return;
    ++frame;
    // Fast forward runs at twice the speed and shows every second picture.
    if (!ff.active || frame % 2 == 0) {
      present(scr);
      ++shown_frames;
    }
    // Test screenshots need the picture as RGB.
    if (script_at < script.size() && script[script_at].frame <= frame &&
        script[script_at].what == "shot")
      scr.compose(rgb.data());
    uint64_t t1 = now_us();
    compose_us += t1 - t0;
    poll();
    if (menu_wanted) {
      menu_wanted = false;
      run_menu();
    }
    if (quit)
      throw core::QuitGame();
    // The games run at the PC-98's 56.4 Hz screen rate.
    const uint64_t period = uint64_t(1e6 / core::kVsyncHz) / (ff.active ? 2 : 1);
    uint64_t now = now_us();
    if (!next)
      next = now;
    next += period;
    if (next > now) {
      sceKernelDelayThread(SceUInt(next - now));
    } else {
      ++late_frames;
      if (now - next > period * 8)
        next = now; // far behind (a long load): do not race to catch up
    }
    ++window_frames;
    uint64_t wall = now_us() - window0;
    if (wall >= 500000) {
      double fps = shown_frames * 1e6 / wall;
      stats(window_frames * 1e6 / wall, (game_us + compose_us) / 1e3 / window_frames,
            compose_us / 1e3 / window_frames);
      (void)fps;
      static uint64_t logged = 0;
      if (now_us() - logged >= 5000000) {
        logged = now_us();
        stat_line("frame %ld: %.1f fps, shown %.1f, game %.2f ms, draw %.2f ms, late %ld%s", frame,
                  window_frames * 1e6 / wall, screen_draws.exchange(0) * 1e6 / wall,
                  game_us / 1e3 / window_frames, compose_us / 1e3 / window_frames, late_frames,
                  ff.active ? ", fast forward" : "");
      }
      window0 = now_us();
      window_frames = late_frames = shown_frames = 0;
      compose_us = game_us = 0;
    }
    last_return = now_us();
  }
  core::InputState input() override { return in; }
  void sound(const core::SoundRequest &req) override {
    if (auto *p = player.load())
      p->push(req);
  }
  bool fastForward() override { return ff.active; }
};

// The HardRPG menu over the paused game: the launcher's own page for this
// game (Resume, Controls, Display, Audio, Performance and speed, Back to
// library), drawn by the launcher's view into pixels and shown over the
// last picture.
void draw_game_into(vita2d_texture *target) {
  const Slot &s = slots[shown_slot];
  const Placement &p = placements[placement_at.load()];
  vita2d_texture_set_filters(s.tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
  vita2d_start_drawing_advanced(target, 0);
  vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
  vita2d_clear_screen();
  vita2d_draw_texture_scale(s.tex, p.x, p.y, p.w / kW, p.h / kH);
  vita2d_end_drawing();
}

void VitaHost::run_menu() {
  hardrpg::GameMenu menu;
  if (!menu.open("ux0:/data/hardrpg/config/" + game_key + ".json")) {
    stat_line("the menu could not open (the game is not the launcher's last one)");
    return;
  }
  audio_paused.store(true);
  MenuPainter painter;
  {
    DrawLock lock;
    painter.blur(draw_game_into);
  }
  hardrpg::GameMenu::Exit exit = hardrpg::GameMenu::Staying;
  while (exit == hardrpg::GameMenu::Staying) {
    menu.step();
    if (menu.takeChanged()) {
      try {
        apply(settings_from(json5pp::parse5(menu.valuesJson())));
      } catch (...) {
      }
      profile = controls_for(game_key);
      place_icon(false);
    }
    exit = menu.exit();
    if (exit != hardrpg::GameMenu::Staying)
      break;
    const hardrpg::View *view = menu.frame();
    // Test script runs (Vita3K): note the menu opened and resume.
    if (!script.empty() && view) {
      stat_line("frame %ld: menu open, %d quads", frame, int(view->scene.size()));
      break;
    }
    {
      DrawLock lock;
      vita2d_start_drawing();
      vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
      vita2d_clear_screen();
      if (view)
        painter.draw(*view);
      vita2d_end_drawing();
      vita2d_swap_buffers();
    }
    // One picture a screen refresh, as the launcher.
    sceDisplayWaitVblankStart();
  }
  menu.close();
  {
    // Back to the game's own picture before it runs on.
    DrawLock lock;
    painter.release();
  }
  place_icon(false);
  audio_paused.store(false);
  controls.reset();
  wait_release = true;
  next = 0;
  menu_since = return_since = 0;
  if (exit == hardrpg::GameMenu::BackToLibrary)
    quit = true;
}

// ---------------------------------------------------------------- game files
// Copies the game out of its disk image once; the copy is reused while the
// image stays the same size and date.
std::string image_stamp(const std::string &disk, const std::string &folder) {
  SceIoStat st;
  if (sceIoGetstat(disk.c_str(), &st) < 0)
    throw std::runtime_error("The game disk is missing: " + disk);
  char buf[160];
  std::snprintf(buf, sizeof buf, "%lld %u-%u-%u %u:%u:%u ", (long long)st.st_size,
                st.st_mtime.year, st.st_mtime.month, st.st_mtime.day, st.st_mtime.hour,
                st.st_mtime.minute, st.st_mtime.second);
  return buf + folder;
}

void extract_tree(hardrpg::Pc98Fat &fat, uint32_t cluster, const std::string &dest,
                  const std::string &title, int &files) {
  mkdirs(dest);
  std::vector<uint8_t> data;
  for (auto &e : fat.list(cluster)) {
    std::string path = dest + "/" + e.name;
    if (e.dir) {
      if (e.cluster)
        extract_tree(fat, e.cluster, path, title, files);
      continue;
    }
    if (!fat.read(e, data))
      throw std::runtime_error("Cannot read " + e.name + " from the game disk");
    if (!write_bytes(path, data.data(), data.size()))
      throw std::runtime_error("Not enough space to copy the game from its disk");
    if (++files % 25 == 0)
      loading_screen(title, "Copying the game from its disk... " + std::to_string(files) + " files");
  }
}

void remove_tree(const std::string &path) {
  SceUID d = sceIoDopen(path.c_str());
  if (d >= 0) {
    SceIoDirent e;
    while (sceIoDread(d, &e) > 0) {
      std::string name = e.d_name;
      if (name == "." || name == "..")
        continue;
      std::string child = path + "/" + name;
      if (SCE_S_ISDIR(e.d_stat.st_mode))
        remove_tree(child);
      else
        sceIoRemove(child.c_str());
    }
    sceIoDclose(d);
  }
  sceIoRmdir(path.c_str());
}

std::string game_from_image(const hardrpg::OpenDtRequest &req, const std::string &title) {
  std::string dest = req.save + "/game", stamp = image_stamp(req.disk, req.folder);
  if (read_text(req.save + "/game.stamp") == stamp && exists(dest))
    return dest;
  loading_screen(title, "Copying the game from its disk...");
  hardrpg::Pc98Fat fat;
  if (!fat.open(req.disk))
    throw std::runtime_error("The game disk has no PC-98 file system");
  uint32_t cluster = 0;
  if (!req.folder.empty()) {
    hardrpg::Pc98Fat::Entry e;
    if (!fat.find(req.folder, e) || !e.dir)
      throw std::runtime_error("The game was not found on its disk");
    cluster = e.cluster;
  }
  std::string part = dest + ".part";
  remove_tree(part);
  int files = 0;
  extract_tree(fat, cluster, part, title, files);
  remove_tree(dest);
  if (sceIoRename(part.c_str(), dest.c_str()) < 0)
    throw std::runtime_error("Could not finish copying the game");
  write_bytes(req.save + "/game.stamp", stamp.data(), stamp.size());
  stat_line("copied %d files from %s", files, req.disk.c_str());
  return dest;
}

// Saves live in HardRPG's save folder. The first time, saves the game folder
// already has (from playing on a PC) are brought over; a slot HardRPG already
// has is never replaced.
void import_saves(const std::string &game, const std::string &saves) {
  std::string mark = saves + "/.imported";
  if (exists(mark))
    return;
  for (auto &name : core::listDir(game)) {
    std::string up = name;
    for (auto &c : up)
      c = (char)std::toupper((unsigned char)c);
    bool d2 = up.size() == 9 && !up.compare(0, 4, "SAVE") && !up.compare(5, 4, ".DAT");
    bool d1 = up.size() == 10 && !up.compare(0, 9, "SAVEDATA.");
    if (!d2 && !d1)
      continue;
    std::string to = saves + "/" + up;
    std::vector<uint8_t> data;
    if (!exists(to) && core::tryReadFile(game, name, data))
      write_bytes(to, data.data(), data.size());
  }
  write_bytes(mark, "1", 1);
}

bool returning;
void return_to_launcher(const char *from) {
  if (returning)
    return;
  returning = true;
  sceClibPrintf("HardRPG OpenDT: returning to the launcher from %s\n", from);
  std::remove(kRequest);
  char back[] = "--hardrpg-return";
  char *args[] = {back, nullptr};
  int rc = sceAppMgrLoadExec("app0:/eboot.bin", args, nullptr);
  if (rc < 0)
    report("Could not return to HardRPG (LoadExec " + std::to_string(rc) + ").");
  sceKernelExitProcess(0);
}

std::string title_of(const std::string &project) {
  auto slash = project.find_last_of('/');
  return slash == std::string::npos ? project : project.substr(slash + 1);
}

int run(int argc, char **argv) {
  bool play = false;
  for (int i = 0; i < argc; ++i)
    if (argv[i] && !std::strcmp(argv[i], "--hardrpg-play"))
      play = true;
  direct = !play && exists(kDirect);
  if (!play && !direct)
    throw std::runtime_error("The Dante 98 runtime needs a HardRPG game selection");
  auto req = hardrpg::openDtRequest(hardrpg::readEasyRpgRequest(kRequest));
  if (!direct)
    std::remove(kRequest);

  // OpenDT needs little: the CPU stays at the system's normal clock.
  scePowerSetArmClockFrequency(333);

  vita2d_init();
  vita2d_set_clear_color(RGBA8(0, 0, 0, 0xff));
  vita2d_set_vblank_wait(0);
  font = vita2d_load_default_pgf();
  const std::string title = title_of(req.project);
  loading_screen(title, "Starting...");
  mkdirs(req.save);
  mkdirs("ux0:/data/hardrpg/reports");
  std::remove(kStats);

  std::string game = req.disk.empty() ? req.project : game_from_image(req, title);
  if (app::detectGame(game) == core::EngineKind::None)
    throw std::runtime_error("This folder does not hold a Dante 98 game");
  import_saves(game, req.save);
  core::setSaveDir(req.save);
  load_script();

  VitaHost host;
  game_key = req.config;
  host.apply(settings_from(merged_settings(game_key)));
  host.profile = controls_for(game_key);
  core::StartOptions opt;
  auto music = std::make_unique<music::MusicPlayer>(game, kRate);
  std::unique_ptr<core::Engine> engine;
  bool d2 = app::engineForRun(game) == core::EngineKind::D2;
  engine = d2 ? d2::createEngine(host, game, opt) : d1::createEngine(host, game, opt);
  stat_line("game %s (%s, %d built-in messages from the game's RPG.EXE)", game.c_str(),
            d2 ? ("Dante 98 II, RPG.EXE " + d2::buildLabel(game)).c_str() : "Dante 98",
            d2 ? d2::exeTextCount() : 0);

  audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, kGrain, kRate,
                                   SCE_AUDIO_OUT_MODE_STEREO);
  player.store(music.get());
  SceUID athread = -1;
  if (audio_port >= 0) {
    // Music files are read and compiled on this thread when the game asks for
    // them (MusicPlayer::apply); 64 KB was not enough for that.
    athread = sceKernelCreateThread("opendt_audio", audio_thread, 0x10000100 - 10, 0x100000, 0,
                                    SCE_KERNEL_CPU_MASK_USER_1, nullptr);
    if (athread >= 0)
      sceKernelStartThread(athread, 0, nullptr);
  } else {
    stat_line("audio port failed: 0x%08x", audio_port);
  }
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

  draw_lock = sceKernelCreateMutex("opendt_draw", 0, 0, nullptr);
  create_slots();
  host.place_icon(false);
  vita2d_wait_rendering_done();
  screen_sema = sceKernelCreateSema("opendt_screen", 0, 0, 1, nullptr);
  screen_run.store(true);
  sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);
  SceUID sthread = sceKernelCreateThread("opendt_screen", screen_thread, 0x10000100 - 5,
                                         0x40000, 0, SCE_KERNEL_CPU_MASK_USER_2, nullptr);
  if (screen_sema < 0 || sthread < 0 || sceKernelStartThread(sthread, 0, nullptr) < 0)
    throw std::runtime_error("Could not start the game screen");

  host.window0 = now_us();
  std::string failure;
  try {
    engine->run();
  } catch (const core::QuitGame &) {
  } catch (const std::exception &e) {
    failure = e.what();
  }
  stat_line("game ended at frame %ld", host.frame);

  screen_run.store(false);
  sceKernelSignalSema(screen_sema, 1);
  sceKernelWaitThreadEnd(sthread, nullptr, nullptr);
  sceKernelDeleteThread(sthread);
  sceKernelDeleteSema(screen_sema);
  audio_run.store(false);
  if (athread >= 0) {
    sceKernelWaitThreadEnd(athread, nullptr, nullptr);
    sceKernelDeleteThread(athread);
  }
  player.store(nullptr);
  if (audio_port >= 0)
    sceAudioOutReleasePort(audio_port);
  vita2d_wait_rendering_done();
  if (!failure.empty())
    throw std::runtime_error(failure);
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  sceClibPrintf("HardRPG OpenDT backend\n");
  try {
    run(argc, argv);
  } catch (const std::exception &e) {
    report(e.what());
  }
  if (direct)
    sceKernelExitProcess(0);
  return_to_launcher("main");
  return 0;
}

// libcurl (linked for the launcher code the menu uses) asks for the user's
// home folder, which the Vita does not have.
extern "C" void *getpwuid(int) { return nullptr; }
