#pragma once
// Fast forward: while it is on, the game runs at up to twice its frame rate
// and only every second frame is drawn. Game logic counts frames, so events,
// messages and battles simply go faster; a busy map that cannot keep up runs
// as fast as it can, never above 2x.
//
// It is switched by pressing L and R together (on release, so the L+R+Select
// return chord does not switch it) or by any input mapped to Fast forward.
namespace hardrpg {
class FastForward {
  bool comboHeld = false, comboSpoiled = false, mappedHeld = false;
  bool drawNext = false;

public:
  // The launcher setting; off ignores both ways of switching.
  bool allowed = false;
  bool active = false;

  // Once per frame with the physical L, R and Select, and whether an input
  // mapped to Fast forward is held. Returns true when active changed.
  bool update(bool l, bool r, bool select, bool mapped) {
    bool was = active;
    if (!allowed) {
      active = comboHeld = comboSpoiled = mappedHeld = false;
      return was;
    }
    if (mapped && !mappedHeld)
      active = !active;
    mappedHeld = mapped;
    if (l && r) {
      // A mapped L or R already switched it; Select means the return chord.
      if (select || mapped)
        comboSpoiled = true;
      comboHeld = true;
    } else if (comboHeld) {
      if (!comboSpoiled)
        active = !active;
      comboHeld = comboSpoiled = false;
    }
    if (active != was)
      drawNext = true;
    return active != was;
  }
  // Once per frame: true when this frame is not drawn.
  bool skip() {
    if (!active)
      return false;
    drawNext = !drawNext;
    return drawNext;
  }
  int rate(int frameRate) const { return active ? frameRate * 2 : frameRate; }
};
} // namespace hardrpg
