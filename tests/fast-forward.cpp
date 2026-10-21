#include "display/fast-forward.h"
#include "input/vita_controls.h"
#include <cassert>
#include <iostream>
using namespace hardrpg;
int main() {
  FastForward f;
  // Off in the launcher: nothing switches it.
  f.update(true, true, false, false);
  f.update(false, false, false, false);
  f.update(false, false, false, true);
  assert(!f.active && !f.skip() && f.rate(60) == 60);
  f.allowed = true;
  f.update(false, false, false, false);
  // L+R switches on release, not while held.
  assert(!f.update(true, false, false, false));
  assert(!f.update(true, true, false, false) && !f.active);
  assert(f.update(false, true, false, false) && f.active);
  assert(f.rate(60) == 120 && f.rate(40) == 80);
  // Every second frame is drawn, starting with the next one.
  assert(!f.skip() && f.skip() && !f.skip() && f.skip());
  assert(f.update(true, true, false, false) == false);
  assert(f.update(false, false, false, false) && !f.active);
  assert(!f.skip() && !f.skip() && f.rate(60) == 60);
  // The L+R+Select return chord leaves it alone.
  f.update(true, true, false, false);
  f.update(true, true, true, false);
  f.update(false, false, false, false);
  assert(!f.active);
  // A mapped input switches on press, once per press.
  assert(f.update(false, false, false, true) && f.active);
  assert(!f.update(false, false, false, true) && f.active);
  f.update(false, false, false, false);
  assert(f.update(false, false, false, true) && !f.active);
  f.update(false, false, false, false);
  // L mapped to Fast forward: pressing L+R switches once, not twice.
  f.update(true, false, false, true);
  f.update(true, true, false, true);
  f.update(false, false, false, false);
  assert(f.active);
  // Skipping is kept only while it makes the game faster. 60 fps game, so
  // 2x is a frame every 8333 us.
  {
    const double period = 1000000.0 / 120;
    auto run = [&](FastForward &g, double drawn, double skipped, int frames) {
      int undrawn = 0;
      for (int i = 0; i < frames; ++i) {
        bool s = g.skip();
        undrawn += s;
        g.measured(!s, s ? skipped : drawn, period);
      }
      return undrawn;
    };
    FastForward g;
    g.allowed = true;
    g.update(true, true, false, false);
    g.update(false, false, false, false);
    assert(g.active && g.skippingFrames());
    // Held back by its scripts (25 ms of logic, 2 ms of drawing): skipping
    // gains ~4%, so every frame is drawn again.
    run(g, 27000, 25000, 40);
    assert(!g.skippingFrames());
    int undrawn = run(g, 27000, 25000, FastForward::probeEvery);
    assert(undrawn <= FastForward::probeFrames);
    // Drawing is the heavy part (8 ms logic, 10 ms drawing): skipping roughly
    // doubles the speed, so it starts at the next measurement run.
    run(g, 18000, 8000, FastForward::probeEvery + 2 * FastForward::probeFrames);
    assert(g.skippingFrames());
    undrawn = run(g, 18000, 8000, 100);
    assert(undrawn == 50);
    // Light scene: drawing every frame already reaches 2x.
    run(g, 6000, 3000, 60);
    assert(!g.skippingFrames());
    // Switching off and on measures afresh.
    g.update(true, true, false, false);
    g.update(false, false, false, false);
    g.update(true, true, false, false);
    g.update(false, false, false, false);
    assert(g.active && g.skippingFrames());
  }
  // Turning the setting off mid-game stops it.
  f.allowed = false;
  assert(f.update(false, false, false, false) && !f.active);
  // The mapping: Fast forward is offered and pressed like any action, and
  // never collides with an RPG Maker button code.
  assert(controlValid(controlFastForward) && controlPickIndex(controlFastForward) >= 0);
  assert(controlName(controlFastForward) == "Fast forward");
  ControlProfile p;
  p.enabled = true;
  p.buttons[4] = controlFastForward;
  ControlState state;
  ControlSample s;
  s.milliseconds = 100;
  state.update(p, s);
  s.buttons = 1u << 4;
  s.milliseconds += 16;
  auto out = state.update(p, s);
  assert(out.actions[controlFastForward] && !out.actions[16]);
  std::cout << "fast-forward: all checks passed\n";
}
