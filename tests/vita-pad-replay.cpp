// Host test for the pad replay: presses that happen between two slow frames
// must reach the game in order, each as its own press.
#include "input/vita_pad_replay.h"
#include <cassert>
#include <iostream>
#include <string>
using hardrpg::PadReplay;
namespace {
const uint16_t R = 1u << 3, Y = 1u << 10, X = 1u << 2;
// What a game sees: the buttons whose state went from up to down on one update.
struct Game {
  uint16_t previous = 0;
  std::string seen;
  void update(PadReplay &r) {
    uint16_t now = r.deliver();
    uint16_t down = now & uint16_t(~previous);
    if (down & R) seen += 'R';
    if (down & X) seen += 'X';
    if (down & Y) seen += 'Y';
    previous = now;
  }
};
// Samples every 16.7 ms from `from` to `to` with the buttons `at(t)` holds.
template <class F> void samples(PadReplay &r, uint64_t from, uint64_t to, F at) {
  for (uint64_t t = from; t < to; t += 16667)
    r.feed(t, at(t));
}
}
int main() {
  // WWS during a 100 ms frame: R held 0-50 ms, R again 100-150, Y 200-250.
  {
    PadReplay r;
    Game g;
    auto pad = [](uint64_t t) -> uint16_t {
      uint16_t b = 0;
      if (t < 50000 || (t >= 100000 && t < 150000)) b |= R;
      if (t >= 200000 && t < 250000) b |= Y;
      return b;
    };
    samples(r, 0, 1, pad);
    g.update(r);            // first R lands
    samples(r, 16667, 120000, pad);
    g.update(r);            // the slow frame ends at 120 ms
    samples(r, 120000, 400000, pad);
    for (int i = 0; i < 8; ++i) g.update(r);
    assert(g.seen == "RRY");
    assert(!r.pending());
  }
  // A tap that starts and ends between two reads still counts.
  {
    PadReplay r;
    Game g;
    g.update(r);
    r.feed(10000, X);
    r.feed(40000, 0);
    g.update(r);
    assert(g.seen == "X" && r.delivered() == X);
    g.update(r);
    assert(r.delivered() == 0);
  }
  // Order is kept: X pressed 60 ms before R inside one slow frame.
  {
    PadReplay r;
    Game g;
    r.feed(10000, X);
    r.feed(70000, X | R);
    g.update(r);
    assert(g.seen == "X");
    g.update(r);
    assert(g.seen == "XR");
  }
  // A chord (presses 10 ms apart) arrives in the same update.
  {
    PadReplay r;
    Game g;
    r.feed(10000, X);
    r.feed(20000, X | R);
    g.update(r);
    assert(r.delivered() == (X | R));
  }
  // When the game keeps up, the game sees the live state on every update.
  {
    PadReplay r;
    uint16_t b = 0;
    for (int i = 0; i < 200; ++i) {
      b = (i / 3) % 2 ? R : 0;
      r.feed(uint64_t(i) * 16667, b);
      assert(r.deliver() == b);
    }
  }
  // A long load with the pad mashed does not build a backlog without end.
  {
    PadReplay r;
    for (int i = 0; i < 400; ++i)
      r.feed(uint64_t(i) * 16667, i % 2 ? R : 0);
    assert(r.pending() <= r.maxPending);
    assert(r.dropped > 0);
  }
  std::cout << "vita pad replay tests passed\n";
}
