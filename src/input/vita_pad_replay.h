#ifndef VITA_PAD_REPLAY_H
#define VITA_PAD_REPLAY_H
// Keeps every pad press and release that happens between two Input.update
// calls. The Vita samples the buttons about every 16.7 ms and keeps the
// recent samples; a game reads the pad once per frame. When a frame takes
// 100 ms, a quick release and press of the same button (a double tap in a
// combo) happens between two reads and used to be lost.
//
// The replay hands the changes to the game in the order they happened:
// each button changes at most once per update, so a game always sees a
// release before the next press. Presses that came close together (a chord)
// are delivered in the same update; a later press waits for the next update,
// so two quick presses of different buttons keep their order.
// When the game keeps up, every update holds one sample and nothing changes.
#include <cstddef>
#include <cstdint>
#include <deque>

namespace hardrpg {
class PadReplay {
public:
  struct Event {
    uint64_t us;
    uint16_t bit;
    bool down;
  };
  // Presses this close together count as pressed together.
  uint64_t chordUs = 30000;
  // A queue this long means the game is not reading the pad at all (a long
  // load); the backlog is dropped and the game sees the current state.
  size_t maxPending = 32;

  // One pad sample, in time order.
  void feed(uint64_t us, uint16_t buttons) {
    uint16_t diff = buttons ^ fed;
    if (!diff)
      return;
    if (queue.size() + 16 > maxPending) {
      queue.clear();
      state = fed;
      ++dropped;
    }
    for (int i = 0; i < 16; ++i)
      if (diff & (1u << i))
        queue.push_back({us, uint16_t(1u << i), (buttons & (1u << i)) != 0});
    fed = buttons;
  }
  // The buttons the game sees for this update.
  uint16_t deliver() {
    uint16_t changed = 0;
    bool anyDown = false;
    uint64_t firstDown = 0;
    size_t n = 0;
    for (; n < queue.size(); ++n) {
      const Event &e = queue[n];
      if (changed & e.bit)
        break;
      if (e.down) {
        if (anyDown && e.us - firstDown > chordUs)
          break;
        if (!anyDown) {
          anyDown = true;
          firstDown = e.us;
        }
        state |= e.bit;
      } else {
        state &= uint16_t(~e.bit);
      }
      changed |= e.bit;
    }
    queue.erase(queue.begin(), queue.begin() + n);
    if (!queue.empty())
      ++deferredUpdates;
    return state;
  }
  void reset(uint16_t buttons = 0) {
    queue.clear();
    state = fed = buttons;
  }
  size_t pending() const { return queue.size(); }
  uint16_t delivered() const { return state; }
  uint16_t live() const { return fed; }
  // Counters for diagnostics.
  unsigned long deferredUpdates = 0, dropped = 0;

private:
  uint16_t fed = 0, state = 0;
  std::deque<Event> queue;
};
} // namespace hardrpg
#endif
