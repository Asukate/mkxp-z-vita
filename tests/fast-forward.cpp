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
