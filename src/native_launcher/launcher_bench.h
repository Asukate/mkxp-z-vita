#pragma once
// Test builds only (HARDRPG_LAUNCHER_BENCH): drives the home screen by
// itself and writes how long it took to show and to move, then quits.
// Remote input does not reach the launcher, so the bench presses buttons.
//
// ux0:/data/hardrpg/launcher-bench.txt names the view to test ("list",
// "carousel" or "grid"); without it the launcher runs as usual. The report
// goes to ux0:/data/hardrpg/launcher-bench-<view>.txt. The view is not
// saved: the player's preferences stay as they were.
#if defined(__vita__) && defined(HARDRPG_LAUNCHER_BENCH)
#include "ui.h"
#include "view.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <malloc.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include "../vita_startup_timer.h"
#include <string>
#include <vector>

namespace hardrpg {
struct LauncherBench {
  bool on = false;
  int view = -1;
  std::string name;
  std::vector<std::pair<std::string, double>> marks; // ms since the process started
  enum Phase { Settle, Moves, Idle, Switch, SwitchLoad, Done } phase = Settle;
  uint32_t phaseAt = 0, nextAt = 0, switchAt = 0;
  int presses = 0, idleRedraws = 0, settleRedraws = 0, switchRedraws = 0;
  std::vector<double> pressFrames, otherFrames;
  double switchFirst = -1, switchReady = -1;
  bool redraw = false;
  // Taken at the end of the idle phase, before any switch.
  bool loaderAtEnd = false;
  int themeAtEnd = 0, userKB = -1, cdramKB = -1, heapKB = -1;
  void snapshot(const UI &ui, bool artLoader) {
    loaderAtEnd = artLoader;
    themeAtEnd = int(ui.themeSources().size());
    SceKernelFreeMemorySizeInfo mem{};
    mem.size = sizeof mem;
    if (sceKernelGetFreeMemorySize(&mem) >= 0) {
      userKB = mem.size_user / 1024;
      cdramKB = mem.size_cdram / 1024;
    }
    struct mallinfo mi = mallinfo();
    heapKB = int(mi.uordblks / 1024);
  }
  static constexpr int pressCount = 40, pressGap = 120;

  void load() {
    FILE *f = std::fopen("ux0:/data/hardrpg/launcher-bench.txt", "r");
    if (!f)
      return;
    char word[32] = {};
    if (std::fscanf(f, "%31s", word) == 1) {
      name = word;
      view = name == "list" ? Home::List : name == "grid" ? Home::Grid : name == "carousel" ? Home::Carousel : -1;
      on = view >= 0;
    }
    std::fclose(f);
  }
  void mark(const char *what) {
    if (on)
      marks.push_back({what, sceKernelGetProcessTimeWide() / 1000.0});
  }
  // After the preferences are read: the view under test, no guide.
  void apply(UI &ui) {
    if (!on)
      return;
    ui.home.view = view;
    ui.home.position = ui.home.target;
    ui.welcome.active = false;
  }
  void shown() {
    mark("first_frame_shown");
    phase = Settle;
    phaseAt = SDL_GetTicks();
  }
  // The button to press now, if any.
  Button next(UI &ui, bool artLoader) {
    if (!on || phase == Done)
      return Button::None;
    uint32_t now = SDL_GetTicks();
    if (phase == Settle && now - phaseAt >= 2000) {
      phase = Moves;
      phaseAt = nextAt = now;
    }
    if (phase == Moves && now >= nextAt) {
      nextAt = now + pressGap;
      bool back = presses >= pressCount / 2;
      ++presses;
      if (presses >= pressCount) {
        phase = Idle;
        phaseAt = now + 500;
      }
      if (ui.home.list())
        return back ? Button::Up : Button::Down;
      return back ? Button::Left : Button::Right;
    }
    if (phase == Idle && int32_t(now - phaseAt) >= 3000) {
      snapshot(ui, artLoader);
      // From the list: how long the carousel takes to show, then to load.
      if (view == Home::List) {
        phase = Switch;
        switchAt = now;
        ui.setView(Home::Carousel, false);
        redraw = true;
      } else
        phase = Done;
    }
    return Button::None;
  }
  bool takeRedraw() {
    bool r = redraw;
    redraw = false;
    return r;
  }
  // Each drawn frame: input to shown, in ms.
  void frame(double ms, bool pressed, bool loading) {
    if (!on)
      return;
    uint32_t now = SDL_GetTicks();
    if (phase == Settle)
      ++settleRedraws;
    else if (phase == Moves || (phase == Idle && int32_t(now - phaseAt) < 0))
      (pressed ? pressFrames : otherFrames).push_back(ms);
    else if (phase == Idle)
      ++idleRedraws;
    else if (phase == Switch) {
      switchFirst = now - switchAt;
      phase = SwitchLoad;
    } else if (phase == SwitchLoad) {
      ++switchRedraws;
      if (!loading) {
        switchReady = now - switchAt;
        phase = Done;
      } else if (now - switchAt > 15000)
        phase = Done;
    }
  }
  bool done() const { return on && phase == Done; }
  static std::string stats(std::vector<double> v) {
    if (v.empty())
      return "none";
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (double x : v)
      sum += x;
    int over = int(std::count_if(v.begin(), v.end(), [](double x) { return x > 20; }));
    char buf[160];
    std::snprintf(buf, sizeof buf, "n=%d mean=%.1f median=%.1f p95=%.1f max=%.1f ms, >20ms=%d", int(v.size()), sum / v.size(),
                  v[v.size() / 2], v[std::min(v.size() - 1, v.size() * 95 / 100)], v.back(), over);
    return buf;
  }
  void report(const UI &ui, const View &view, bool artLoader, bool releaseStarted, bool releaseDeferred, bool gameInfoStarted) {
    std::string path = "ux0:/data/hardrpg/launcher-bench-" + name + ".txt";
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f)
      return;
    std::fprintf(f, "view %s, games %d\n", name.c_str(), int(ui.model.list.size()));
    const char *phases[48];
    uint64_t us[48];
    int n = vitaStartupTimerBenchMarks(phases, us, 48);
    for (int i = 0; i < n; ++i)
      std::fprintf(f, "boot %-28s %9.1f ms\n", phases[i], us[i] / 1000.0);
    for (auto &m : marks)
      std::fprintf(f, "mark %-22s %9.1f ms\n", m.first.c_str(), m.second);
    std::fprintf(f, "redraws while settling (2 s after shown): %d\n", settleRedraws);
    std::fprintf(f, "press frames (%d presses, %d ms apart): %s\n", pressCount, pressGap, stats(pressFrames).c_str());
    std::fprintf(f, "other frames during presses: %s\n", stats(otherFrames).c_str());
    std::fprintf(f, "redraws while idle (3 s): %d\n", idleRedraws);
    std::fprintf(f, "art loader running after idle: %s\n", loaderAtEnd ? "yes" : "no");
    std::fprintf(f, "title music sources to prepare: %d\n", themeAtEnd);
    std::fprintf(f, "release check: %s\n", releaseStarted ? "started" : releaseDeferred ? "deferred (not started)" : "off");
    std::fprintf(f, "game info lookup: %s\n", gameInfoStarted ? "started" : "not started");
    if (switchFirst >= 0)
      std::fprintf(f, "switch to carousel: first frame %.0f ms, pictures ready %.0f ms (%d redraws)\n", switchFirst, switchReady,
                   switchRedraws);
    std::fprintf(f, "after idle: free user memory %d KB, free cdram %d KB, heap in use %d KB\n", userKB, cdramKB, heapKB);
    std::fclose(f);
    (void)view;
    (void)artLoader;
  }
};
} // namespace hardrpg
#endif
