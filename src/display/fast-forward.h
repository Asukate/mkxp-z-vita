#pragma once
// Fast forward: while it is on, the game runs at up to twice its frame rate.
// Game logic counts frames, so events, messages and battles simply go faster.
//
// Leaving every second frame undrawn frees time for the game's own work, but
// only helps when drawing is a large part of a frame. A game held back by
// its scripts gains almost no speed and would only look half as smooth, so
// the time each kind of frame takes is measured and frames are skipped only
// while skipping makes the game clearly faster. When it does not, every frame
// is drawn and the game runs as fast as it can, never above 2x.
//
// It is switched by pressing L and R together (on release, so the L+R+Select
// return chord does not switch it) or by any input mapped to Fast forward.
namespace hardrpg {
class FastForward {
  bool comboHeld = false, comboSpoiled = false, mappedHeld = false;
  bool drawNext = false;
  // Average work time of a drawn and of an undrawn frame, in microseconds,
  // without the wait for the frame rate. Zero until measured.
  double drawnUs = 0, skippedUs = 0;
  bool skipping = true;
  // While drawing every frame, a short run of skipped frames now and then
  // keeps the undrawn time up to date (a new map can change the balance).
  int sinceProbe = 0, probeLeft = 0;

public:
  static constexpr int probeEvery = 600, probeFrames = 8;
  // Skipping starts when it makes frames this much faster on average, and
  // stops below the lower figure, so it does not flip back and forth.
  static constexpr double startGain = 1.15, keepGain = 1.08;

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
    if (active != was) {
      drawNext = true;
      // Measure afresh: the scene may have changed since the last time.
      drawnUs = skippedUs = 0;
      skipping = true;
      sinceProbe = probeLeft = 0;
    }
    return active != was;
  }
  // Once per frame: true when this frame is not drawn.
  bool skip() {
    if (!active)
      return false;
    bool alternate = skipping || probeLeft > 0;
    if (probeLeft > 0)
      --probeLeft;
    else if (!skipping && ++sinceProbe >= probeEvery) {
      sinceProbe = 0;
      probeLeft = probeFrames;
      skippedUs = 0; // judged on this run alone
    }
    if (!alternate) {
      drawNext = true;
      return false;
    }
    drawNext = !drawNext;
    return drawNext;
  }
  // After each frame while active: how long its work took, and the frame
  // period at twice the game's rate.
  void measured(bool drawn, double us, double periodUs) {
    double &average = drawn ? drawnUs : skippedUs;
    average = average > 0 ? average * 0.9 + us * 0.1 : us;
    if (drawnUs <= 0 || skippedUs <= 0)
      return;
    // Drawing every frame already reaches 2x: nothing to gain.
    if (drawnUs <= periodUs) {
      skipping = false;
      return;
    }
    double gain = drawnUs / ((drawnUs + skippedUs) / 2);
    skipping = gain >= (skipping ? keepGain : startGain);
  }
  bool skippingFrames() const { return active && skipping; }
  int rate(int frameRate) const { return active ? frameRate * 2 : frameRate; }
};
} // namespace hardrpg
