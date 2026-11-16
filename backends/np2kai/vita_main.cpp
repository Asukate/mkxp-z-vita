// SPDX-License-Identifier: GPL-3.0-or-later
// HardRPG PC-98 backend (app0:/np2kai.self): runs NP2kai for games made with
// RPG Maker Dante 98 / Dante 98 II and other PC-98 disk images. Started by
// the launcher with --hardrpg-play and a request in selection.json; returns
// to the launcher with --hardrpg-return.
#include "frontend.h"
#include "libretro.h"
#include "native_launcher/np2kai_handoff.h"

#include <psp2/apputil.h>
#include <psp2/appmgr.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <vita2d.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int _newlib_heap_size_user = 192 * 1024 * 1024;

namespace {
constexpr const char *kRequest = "ux0:/data/hardrpg/selection.json";
constexpr const char *kShared = "ux0:/data/hardrpg/np2kai"; // optional bios.rom, font.bmp...
constexpr const char *kStats = "ux0:/data/hardrpg/reports/np2kai-stats.txt";

void report(const std::string &message) {
  if (FILE *f = std::fopen("ux0:/data/hardrpg/last-error.txt", "wb")) {
    std::fprintf(f, "PC-98 (NP2kai)\n%s\n", message.c_str());
    std::fclose(f);
  }
  sceClibPrintf("HardRPG NP2kai error: %s\n", message.c_str());
}

void append_stats(const char *line) {
  if (FILE *f = std::fopen(kStats, "ab")) {
    std::fprintf(f, "%s\n", line);
    std::fclose(f);
  }
}

// Writing to the memory card can stall for a few hundred milliseconds, long
// enough to empty the sound queue, so during play the speed log is written
// by a low-priority thread. One writer (the machine thread), one reader.
constexpr int kLogLines = 16;
char log_lines[kLogLines][512];
std::atomic<uint32_t> log_head{0}, log_tail{0};
std::atomic<bool> log_run{false};

int log_thread(SceSize, void *) {
  for (;;) {
    bool running = log_run.load();
    while (log_tail.load() != log_head.load()) {
      append_stats(log_lines[log_tail.load() % kLogLines]);
      log_tail.fetch_add(1);
    }
    if (!running)
      return 0;
    sceKernelDelayThread(200000);
  }
}

void stat_line(const char *fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  sceClibPrintf("HardRPG NP2kai: %s\n", line);
  if (!log_run.load()) {
    append_stats(line);
    return;
  }
  uint32_t h = log_head.load();
  if (h - log_tail.load() >= kLogLines)
    return; // the card is far behind: drop the line rather than wait
  std::memcpy(log_lines[h % kLogLines], line, sizeof line);
  log_head.store(h + 1);
}

uint64_t now_us() { return sceKernelGetProcessTimeWide(); }

bool exists(const std::string &path) {
  SceIoStat st;
  return sceIoGetstat(path.c_str(), &st) >= 0;
}
int64_t file_size(const std::string &path) {
  SceIoStat st;
  return sceIoGetstat(path.c_str(), &st) >= 0 ? st.st_size : -1;
}
void mkdirs(const std::string &path) {
  for (size_t i = path.find('/', path.find(":/") + 2); i != std::string::npos;
       i = path.find('/', i + 1))
    sceIoMkdir(path.substr(0, i).c_str(), 0777);
  sceIoMkdir(path.c_str(), 0777);
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

// Copies src to dst through a temporary file, showing progress.
void copy_file(const std::string &src, const std::string &dst, const std::string &title) {
  FILE *in = std::fopen(src.c_str(), "rb");
  if (!in)
    throw std::runtime_error("Cannot open " + src);
  std::string tmp = dst + ".part";
  FILE *out = std::fopen(tmp.c_str(), "wb");
  if (!out) {
    std::fclose(in);
    throw std::runtime_error("Cannot write " + tmp);
  }
  int64_t total = file_size(src), done = 0;
  std::vector<char> buf(1 << 20);
  for (;;) {
    size_t n = std::fread(buf.data(), 1, buf.size(), in);
    if (n == 0)
      break;
    if (std::fwrite(buf.data(), 1, n, out) != n) {
      std::fclose(in);
      std::fclose(out);
      std::remove(tmp.c_str());
      throw std::runtime_error("Not enough space to copy the game disk");
    }
    done += n;
    char line[96];
    std::snprintf(line, sizeof line, "Preparing the game disk... %d%%",
                  total > 0 ? int(done * 100 / total) : 0);
    loading_screen(title, line);
  }
  bool ok = !std::ferror(in);
  std::fclose(in);
  ok = std::fclose(out) == 0 && ok;
  if (!ok || sceIoRename(tmp.c_str(), dst.c_str()) < 0) {
    std::remove(tmp.c_str());
    throw std::runtime_error("Could not copy the game disk");
  }
}

// ---------------------------------------------------------------- audio
constexpr int kRing = 1 << 15; // stereo frames
int16_t ring[kRing * 2];
std::atomic<uint32_t> ring_read{0}, ring_write{0};
std::atomic<bool> audio_run{true};
std::atomic<uint32_t> underruns{0}, audio_errors{0};
std::atomic<int> audio_last_error{0};
std::atomic<uint32_t> audio_min_queue{~0u}; // lowest queue the sound thread saw
int audio_port = -1;
constexpr int kGrain = 512;

uint32_t queued() { return ring_write.load() - ring_read.load(); }

void push_audio(const int16_t *s, size_t frames, void *) {
  uint32_t w = ring_write.load();
  for (size_t i = 0; i < frames; ++i) {
    if (w - ring_read.load() >= kRing)
      break; // full: drop
    uint32_t at = (w & (kRing - 1)) * 2;
    ring[at] = s[i * 2];
    ring[at + 1] = s[i * 2 + 1];
    ++w;
  }
  ring_write.store(w);
}
void drop_audio(const int16_t *, size_t, void *) {}

int audio_thread(SceSize, void *) {
  static int16_t out[kGrain * 2];
  while (audio_run.load()) {
    uint32_t r = ring_read.load(), have = ring_write.load() - r;
    if (have < audio_min_queue.load())
      audio_min_queue.store(have);
    if (have >= kGrain) {
      for (int i = 0; i < kGrain; ++i) {
        uint32_t at = ((r + i) & (kRing - 1)) * 2;
        out[i * 2] = ring[at];
        out[i * 2 + 1] = ring[at + 1];
      }
      ring_read.store(r + kGrain);
    } else {
      std::memset(out, 0, sizeof out);
      underruns.fetch_add(1);
    }
    int rc = sceAudioOutOutput(audio_port, out);
    if (rc < 0) {
      // The system can refuse sound for a moment; wait one grain instead of
      // spinning through the queue.
      audio_errors.fetch_add(1);
      audio_last_error.store(rc);
      sceKernelDelayThread(kGrain * 1000000 / 44100);
    }
  }
  return 0;
}

// ---------------------------------------------------------------- input
struct Mapping {
  uint32_t button;
  unsigned key;
};
const Mapping kMap[] = {
    {SCE_CTRL_UP, RETROK_UP},         {SCE_CTRL_DOWN, RETROK_DOWN},
    {SCE_CTRL_LEFT, RETROK_LEFT},     {SCE_CTRL_RIGHT, RETROK_RIGHT},
    {SCE_CTRL_CROSS, RETROK_RETURN},  {SCE_CTRL_CIRCLE, RETROK_ESCAPE},
    {SCE_CTRL_SQUARE, RETROK_SPACE},  {SCE_CTRL_TRIANGLE, RETROK_ESCAPE},
    {SCE_CTRL_START, RETROK_ESCAPE},  {SCE_CTRL_SELECT, RETROK_F1},
};
constexpr uint32_t kReturnChord = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT;

uint32_t read_buttons() {
  SceCtrlData pad{};
  sceCtrlPeekBufferPositive(0, &pad, 1);
  uint32_t b = pad.buttons;
  // Left stick works as the d-pad.
  if (pad.ly < 64) b |= SCE_CTRL_UP;
  if (pad.ly > 192) b |= SCE_CTRL_DOWN;
  if (pad.lx < 64) b |= SCE_CTRL_LEFT;
  if (pad.lx > 192) b |= SCE_CTRL_RIGHT;
  return b;
}

void apply_input(uint32_t b) {
  bool chord = (b & kReturnChord) == kReturnChord;
  // Several buttons share a key: a key is down while any of them is held.
  unsigned char down[RETROK_LAST] = {};
  if (!chord)
    for (const auto &m : kMap)
      if (b & m.button)
        down[m.key] = 1;
  for (const auto &m : kMap)
    np2fe_set_key(m.key, down[m.key]);
}

// ---------------------------------------------------------------- video
// Pictures go to the screen on their own thread: handing a frame to the
// display waits for a free screen buffer, and the machine must not wait for
// that. Three picture slots: the machine fills one, one waits as the newest,
// and the screen thread draws the third.
constexpr unsigned kMaxW = 640, kMaxH = 480;
struct Slot {
  vita2d_texture *tex = nullptr;
  unsigned w = 0, h = 0;
  char overlay[128] = "";
};
Slot slots[3];
// Lock-free triple buffer: the machine owns fill_slot, the screen thread owns
// shown_slot, and middle holds the third slot plus a "new picture" bit.
constexpr int kFresh = 4;
std::atomic<int> middle{1};
int fill_slot = 0, shown_slot = 2;
std::atomic<bool> screen_run{false};
std::atomic<uint32_t> screen_draws{0};
SceUID screen_sema = -1;

void draw_slot(const Slot &s) {
  vita2d_start_drawing();
  vita2d_clear_screen();
  if (s.w && s.h) {
    // PC-98 monitors are 4:3: 640x400 pixels are taller than they are wide.
    float h = 544.f, w = h * 4.f / 3.f;
    vita2d_draw_texture_part_scale(s.tex, (960.f - w) / 2.f, 0, 0, 0, s.w, s.h,
                                   w / s.w, h / s.h);
  }
  if (s.overlay[0])
    vita2d_pgf_draw_text(font, 8, 536, RGBA8(0xff, 0xff, 0x80, 0xff), 0.8f, s.overlay);
  vita2d_end_drawing();
  vita2d_swap_buffers();
}

// Shows each new picture at the next screen refresh. vita2d does not wait for
// the refresh here (that made every picture take two), so the thread waits
// itself only when the last picture has not reached the screen yet; then the
// buffer it draws into is never the one being shown.
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

void create_slots(bool xrgb8888) {
  for (auto &s : slots) {
    s.tex = vita2d_create_empty_texture_format(
        kMaxW, kMaxH,
        xrgb8888 ? SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1RGB : SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB);
    if (!s.tex)
      throw std::runtime_error("Not enough video memory for the PC-98 screen");
    vita2d_texture_set_filters(s.tex, SCE_GXM_TEXTURE_FILTER_LINEAR,
                               SCE_GXM_TEXTURE_FILTER_LINEAR);
  }
}

// Copies the machine's latest picture into a free slot and hands it to the
// screen thread. Never waits.
void present(const np2fe_frame *f, const char *overlay) {
  if (!f->pixels || f->width > kMaxW || f->height > kMaxH)
    return;
  Slot &s = slots[fill_slot];
  unsigned stride = vita2d_texture_get_stride(s.tex);
  size_t row = f->width * (f->xrgb8888 ? 4 : 2);
  auto *dst = static_cast<unsigned char *>(vita2d_texture_get_datap(s.tex));
  auto *src = static_cast<const unsigned char *>(f->pixels);
  for (unsigned y = 0; y < f->height; ++y)
    std::memcpy(dst + y * stride, src + y * f->pitch, row);
  s.w = f->width;
  s.h = f->height;
  std::snprintf(s.overlay, sizeof s.overlay, "%s", overlay ? overlay : "");
  fill_slot = middle.exchange(fill_slot | kFresh) & 3;
  sceKernelSignalSema(screen_sema, 1);
}

bool frame_blank(const np2fe_frame *f) {
  if (!f->pixels)
    return true;
  auto *p = static_cast<const unsigned char *>(f->pixels);
  for (unsigned y = 0; y < f->height; y += 8)
    for (unsigned x = 0; x < f->width; x += 8) {
      const unsigned char *px = p + y * f->pitch + x * (f->xrgb8888 ? 4 : 2);
      uint32_t v = f->xrgb8888 ? (*(const uint32_t *)px & 0xffffff) : *(const uint16_t *)px;
      if (v)
        return false;
    }
  return true;
}

bool returning;
void return_to_launcher(const char *from) {
  if (returning)
    return;
  returning = true;
  sceClibPrintf("HardRPG NP2kai: returning to the launcher from %s\n", from);
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
  if (!play)
    throw std::runtime_error("The PC-98 runtime needs a HardRPG game selection");
  auto req = hardrpg::np2kaiRequest(hardrpg::readEasyRpgRequest(kRequest));
  std::remove(kRequest);
  if (!exists(req.disk))
    throw std::runtime_error("The game disk is missing: " + req.disk);

  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  vita2d_init();
  vita2d_set_clear_color(RGBA8(0, 0, 0, 0xff));
  vita2d_set_vblank_wait(0);
  font = vita2d_load_default_pgf();
  const std::string title = title_of(req.project);
  loading_screen(title, "Starting...");

  // Saves live on the disk image, so the game runs from its own copy and the
  // original stays untouched. The copy is made once.
  mkdirs(req.save + "/system/np2kai");
  mkdirs("ux0:/data/hardrpg/reports");
  auto dot = req.disk.find_last_of('.');
  std::string ext = dot == std::string::npos ? ".hdi" : req.disk.substr(dot);
  for (auto &c : ext)
    c = (char)std::tolower((unsigned char)c);
  std::string disk = req.save + "/disk" + ext;
  if (!exists(disk))
    copy_file(req.disk, disk, title);
  std::string sys = req.save + "/system";
  // Shared machine files the player may add (bios.rom, sound.rom, font.bmp).
  for (const char *name : {"bios.rom", "sound.rom", "itf.rom", "bios9821.rom",
                           "font.bmp", "FONT.ROM", "2608_bd.wav", "2608_sd.wav",
                           "2608_top.wav", "2608_hh.wav", "2608_tom.wav", "2608_rim.wav"}) {
    std::string from = std::string(kShared) + "/" + name, to = sys + "/np2kai/" + name;
    if (exists(from) && file_size(from) != file_size(to))
      copy_file(from, to, title);
  }
  // A font shipped with the game (anex86.bmp / font.bmp) wins.
  if (!req.font.empty() && exists(req.font) &&
      file_size(req.font) != file_size(sys + "/np2kai/font.bmp"))
    copy_file(req.font, sys + "/np2kai/font.bmp", title);

  std::vector<np2fe_option> options;
  for (auto &o : req.options)
    options.push_back({o.first.c_str(), o.second.c_str()});
  auto set_default = [&](const char *key, const char *value) {
    for (auto &o : options)
      if (!std::strcmp(o.key, key))
        return;
    options.push_back({key, value});
  };
  // The Vita cannot emulate a fast-clocked PC-98 in real time: at 32x a frame
  // took 23 ms of the 17.7 ms budget, and at 8x busy scenes still took 20 ms,
  // which breaks up the sound. At 6x (14.7 MHz) busy scenes take about 16 ms,
  // so sound stays smooth; the busiest animations play a little slower, as on
  // a slower PC-98. ux0:/data/hardrpg/np2kai/clk_mult.txt overrides the cap
  // for testing.
  int cap = 6;
  if (FILE *f = std::fopen((std::string(kShared) + "/clk_mult.txt").c_str(), "rb")) {
    int v = 0;
    if (std::fscanf(f, "%d", &v) == 1 && v >= 1 && v <= 64)
      cap = v;
    std::fclose(f);
  }
  static std::string capped;
  bool has_mult = false;
  for (auto &o : options)
    if (!std::strcmp(o.key, "np2kai_clk_mult")) {
      has_mult = true;
      if (std::atoi(o.value) > cap) {
        capped = std::to_string(cap);
        o.value = capped.c_str();
      }
    }
  if (!has_mult) {
    capped = std::to_string(std::min(cap, 6));
    options.push_back({"np2kai_clk_mult", capped.c_str()});
  }
  set_default("np2kai_FastMC", "ON");
  set_default("np2kai_joymode", "OFF");
  set_default("np2kai_keyboard", "Ja");
  options.push_back({nullptr, nullptr});

  std::remove(kStats);
  np2fe_config cfg{};
  cfg.system_dir = sys.c_str();
  cfg.save_dir = req.save.c_str();
  cfg.content = disk.c_str();
  cfg.options = options.data();
  cfg.audio = drop_audio;
  cfg.log = [](const char *line, void *) { sceClibPrintf("%s\n", line); };
  for (auto &o : options)
    if (o.key)
      stat_line("option %s=%s", o.key, o.value);
  if (np2fe_start(&cfg))
    throw std::runtime_error("NP2kai could not start the game");

  audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, kGrain,
                                   (int)np2fe_sample_rate(), SCE_AUDIO_OUT_MODE_STEREO);
  if (audio_port < 0)
    stat_line("audio port failed: 0x%08x", audio_port);
  SceUID athread = sceKernelCreateThread("np2kai_audio", audio_thread, 0x10000100 - 10,
                                         0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, nullptr);
  if (audio_port >= 0 && athread >= 0)
    sceKernelStartThread(athread, 0, nullptr);
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

  // Hidden fast boot: DOS and the machine's startup run at full speed behind
  // a loading screen, until the game turns on graphics and draws something.
  uint64_t boot0 = now_us();
  long boot_frames = 0, after_gfx = -1;
  const long kBootCap = 56 * 60; // give up hiding the boot after a minute of machine time
  for (; boot_frames < kBootCap; ++boot_frames) {
    np2fe_run_frame();
    if (after_gfx < 0 && np2fe_graphics_on())
      after_gfx = 0;
    if (after_gfx >= 0 && (++after_gfx > 600 || !frame_blank(np2fe_current_frame())))
      break;
    if (boot_frames % 30 == 0)
      loading_screen(title, "Starting the PC-98...");
    if ((read_buttons() & kReturnChord) == kReturnChord) {
      np2fe_stop();
      return_to_launcher("boot");
    }
  }
  stat_line("boot: %ld machine frames in %.2f s (graphics after %s)", boot_frames,
            (now_us() - boot0) / 1e6, after_gfx >= 0 ? "yes" : "no");

  // Normal play, paced by the audio clock. Frames run until about 100 ms of
  // sound is queued (up to four per pass, so a slow frame is caught up
  // without waiting for the next screen refresh); then the newest picture is
  // shown. The cushion keeps a busy moment from breaking up the sound.
  np2fe_set_audio(push_audio);
  np2fe_take_io_stats();
  create_slots(np2fe_current_frame()->xrgb8888);
  vita2d_wait_rendering_done();
  screen_sema = sceKernelCreateSema("np2kai_screen", 0, 0, 1, nullptr);
  screen_run.store(true);
  // Threads get a core each: the machine on core 0, sound on 1, screen on 2.
  // The screen thread ranks above the machine so a busy machine never
  // starves it; it spends most of its time waiting for the next picture.
  sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);
  SceUID sthread = sceKernelCreateThread("np2kai_screen", screen_thread, 0x10000100 - 5,
                                         0x10000, 0, SCE_KERNEL_CPU_MASK_USER_2, nullptr);
  if (screen_sema < 0 || sthread < 0 || sceKernelStartThread(sthread, 0, nullptr) < 0)
    throw std::runtime_error("Could not start the PC-98 screen");
  log_run.store(true);
  SceUID lthread = sceKernelCreateThread("np2kai_log", log_thread, 0x10000100 + 20, 0x4000, 0,
                                         SCE_KERNEL_CPU_MASK_USER_2, nullptr);
  if (lthread < 0 || sceKernelStartThread(lthread, 0, nullptr) < 0)
    log_run.store(false);
  const uint32_t target = (uint32_t)(np2fe_sample_rate() / 10); // 100 ms
  const uint32_t per_frame = (uint32_t)(np2fe_sample_rate() / np2fe_fps()) + 1;
  ring_read.store(ring_write.load()); // drop anything left from the boot
  underruns.store(0);
  bool was_fast = false;
  uint64_t window0 = now_us(), core_us = 0, core_max = 0, pass_prev = 0, pass_gap_max = 0;
  long window_frames = 0, total_frames = 0;
  bool show_stats = false;
  uint32_t last_buttons = 0;
  char overlay[128] = "";
  for (;;) {
    uint32_t b = read_buttons();
    if ((b & kReturnChord) == kReturnChord)
      break;
    {
      uint64_t t = now_us();
      if (pass_prev)
        pass_gap_max = std::max(pass_gap_max, t - pass_prev);
      pass_prev = t;
    }
    // L + R + Triangle shows the speed counter.
    uint32_t stats_chord = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_TRIANGLE;
    if ((b & stats_chord) == stats_chord && (last_buttons & stats_chord) != stats_chord)
      show_stats = !show_stats;
    last_buttons = b;
    bool fast = (b & SCE_CTRL_RTRIGGER) && !(b & SCE_CTRL_LTRIGGER); // hold R: fast forward
    if (fast != was_fast) // fast forward is silent, so no backlog of sound builds up
      np2fe_set_audio(fast ? drop_audio : push_audio);
    was_fast = fast;
    apply_input(b);
    // One frame per picture. Only when the sound queue runs low (under 50 ms)
    // does a pass run one extra frame without drawing it, so the machine
    // regains time while the screen keeps moving.
    int ran = 0;
    const int max_run = fast ? 4 : queued() < target / 2 ? 2 : 1;
    while (ran < max_run && (fast || audio_port < 0 || queued() < target)) {
      np2fe_set_draw(ran == max_run - 1 || (!fast && queued() + per_frame >= target));
      uint64_t t0 = now_us();
      np2fe_run_frame();
      uint64_t dt = now_us() - t0;
      core_us += dt;
      core_max = std::max(core_max, dt);
      ++ran;
      ++window_frames;
      ++total_frames;
      if (audio_port < 0 && !fast)
        break; // no sound clock: one frame per screen refresh
    }
    np2fe_set_draw(1);
    if (ran == 0) {
      sceKernelDelayThread(2000);
      continue;
    }
    present(np2fe_current_frame(), show_stats ? overlay : nullptr);
    uint64_t wall = now_us() - window0;
    if (wall >= 5000000) {
      double fps = window_frames * 1e6 / wall;
      np2fe_io_stats io = np2fe_take_io_stats();
      std::snprintf(overlay, sizeof overlay, "%.1f fps  core %.1f ms  max %.1f ms",
                    fps, core_us / 1e3 / window_frames, core_max / 1e3);
      stat_line("play: %.1f fps (target %.1f), shown %.1f, core avg %.2f ms max %.2f ms, "
                "underruns %u, min queue %u, longest pass %.1f ms, sound errors %u (last 0x%08x), queued %u, card read %.1f ms %llu B, write %.1f ms %llu B, ram %llu B",
                fps, np2fe_fps(), screen_draws.exchange(0) * 1e6 / wall,
                core_us / 1e3 / window_frames, core_max / 1e3, (unsigned)underruns.exchange(0),
                (unsigned)audio_min_queue.exchange(~0u), pass_gap_max / 1e3,
                (unsigned)audio_errors.exchange(0), (unsigned)audio_last_error.load(),
                (unsigned)queued(),
                io.read_us / 1e3, (unsigned long long)io.read_bytes, io.write_us / 1e3,
                (unsigned long long)io.write_bytes, (unsigned long long)io.mem_read_bytes);
      window0 = now_us();
      window_frames = 0;
      core_us = core_max = pass_gap_max = 0;
    }
  }
  if (log_run.exchange(false)) {
    sceKernelWaitThreadEnd(lthread, nullptr, nullptr);
    sceKernelDeleteThread(lthread);
  }
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
  if (audio_port >= 0)
    sceAudioOutReleasePort(audio_port);
  np2fe_release_all_keys();
  np2fe_stop(); // writes the disk and the machine settings
  vita2d_wait_rendering_done();
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  sceClibPrintf("HardRPG NP2kai backend\n");
  try {
    run(argc, argv);
  } catch (const std::exception &e) {
    report(e.what());
  }
  return_to_launcher("main");
  return 0;
}
