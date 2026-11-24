#pragma once
// The popup shown over a running game when an achievement is earned: a glass
// card that slides down from the top edge, stays a few seconds and slides
// back up. A swipe (up or sideways) sends it away early; a short drag springs
// back. Earned at once, several wait their turn. Times are milliseconds from
// any steady clock; positions are on the 960x544 screen.
//
// Only the motion lives here (no drawing), so the desktop tests and renders
// run the same code as the Vita: View::achievementToast draws the card, the
// game layer (display/game-menu-layer.h) puts it over its frosted glass.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <string>

namespace hardrpg {
class AchievementToasts {
public:
  struct Toast {
    std::string name, description, icon; // icon: a file, empty for none
  };
  static constexpr int width = 452, height = 80, top = 12;
  static constexpr int left = (960 - width) / 2;
  static constexpr uint64_t enterMs = 340, holdMs = 4500, leaveMs = 260, settleMs = 200;
  // Where the card is drawn this frame.
  struct Placement {
    float x = left, y = top;
    float alpha = 1; // the card's contents; the glass under it slides only
  };

  void push(Toast t) {
    if (queue.size() < 32)
      queue.push_back(std::move(t));
  }
  bool active() const { return !queue.empty(); }
  const Toast &current() const { return queue.front(); }
  // Whether a finger holds the card.
  bool holding() const { return phase == Dragging; }

  // Moves the clock on; the shown card leaves when its time is up.
  void update(uint64_t now) {
    if (queue.empty())
      return;
    if (phase == Idle) {
      phase = Entering;
      since = now;
      dx = dy = 0;
    }
    if (phase == Entering && now - since >= enterMs) {
      phase = Showing;
      since = now;
      shownFor = 0;
    }
    if (phase == Settling && now - since >= settleMs) {
      phase = Showing;
      since = now;
      dx = dy = 0;
    }
    if (phase == Showing && shownFor + (now - since) >= holdMs)
      leave(now, 0, -1);
    if (phase == Leaving && now - since >= leaveMs) {
      queue.pop_front();
      phase = Idle;
      update(now);
    }
  }

  // A front touch this frame. began: the finger just went down. On the frame
  // it lifts, pass down false with its last position. Returns true while the
  // card holds the touch (the game must not see it).
  bool touch(bool down, bool began, int x, int y, uint64_t now) {
    update(now);
    if (queue.empty())
      return false;
    if (phase == Dragging) {
      if (down) {
        dx = float(x - startX);
        dy = float(y - startY);
        if (now - lastAt > 40)
          lastX = x, lastY = y, lastAt = now;
        return true;
      }
      // Let go: far enough (or flicked) and it goes; otherwise it settles.
      const float speed = now > lastAt ? float(std::hypot(x - lastX, y - lastY)) / float(now - lastAt) : 0;
      if (dy < -22 || std::fabs(dx) > 90 || (speed > 0.6f && (dy < -8 || std::fabs(dx) > 30))) {
        if (std::fabs(dx) > -dy)
          leave(now, dx > 0 ? 1 : -1, 0);
        else
          leave(now, 0, -1);
      } else {
        phase = Settling;
        since = now;
      }
      return false;
    }
    if (!down || !began || (phase != Showing && phase != Entering && phase != Settling))
      return false;
    const auto p = placement(now);
    if (x < p.x || x >= p.x + width || y < p.y || y >= p.y + height)
      return false;
    // Caught: the time already shown is kept, it starts again from there.
    if (phase == Showing)
      shownFor += now - since;
    phase = Dragging;
    startX = x - int(dx), startY = y - int(dy);
    lastX = x, lastY = y, lastAt = now;
    return true;
  }
  Placement placement(uint64_t now) const {
    Placement p;
    if (queue.empty())
      return p;
    const float t = float(now - since);
    auto easeOut = [](float k) { return 1 - (1 - k) * (1 - k) * (1 - k); };
    switch (phase) {
    case Idle:
    case Entering: {
      // Down from above the screen, a touch past its place and back.
      float k = std::min(1.f, t / enterMs);
      float e = easeOut(k) + 0.06f * std::sin(k * 3.14159f);
      p.y = -height - 8 + (top + height + 8) * e;
      p.alpha = std::min(1.f, k * 1.6f);
      break;
    }
    case Showing:
      break;
    case Dragging:
      // Up and sideways follow the finger; down only stretches a little.
      p.x = left + dx;
      p.y = top + (dy < 0 ? dy : dy * 0.25f);
      p.alpha = 1 - std::min(0.6f, std::max(std::fabs(dx) / 360.f, -dy / 160.f));
      break;
    case Settling: {
      float k = 1 - easeOut(std::min(1.f, t / settleMs));
      p.x = left + dx * k;
      p.y = top + (dy < 0 ? dy : dy * 0.25f) * k;
      break;
    }
    case Leaving: {
      // Swiped away it keeps its speed; on its own it eases up and out.
      float k = std::min(1.f, t / leaveMs), e = swiped ? 1 - (1 - k) * (1 - k) : k * k * (3 - 2 * k);
      p.x = fromX + dirX * (960 - left + 40) * e;
      p.y = fromY + dirY * (fromY + height + 20) * e;
      p.alpha = fromAlpha * (1 - k);
      break;
    }
    }
    return p;
  }

private:
  enum Phase { Idle, Entering, Showing, Dragging, Settling, Leaving } phase = Idle;
  std::deque<Toast> queue;
  uint64_t since = 0, shownFor = 0, lastAt = 0;
  float dx = 0, dy = 0, fromX = left, fromY = top, fromAlpha = 1;
  bool swiped = false;
  int dirX = 0, dirY = -1, startX = 0, startY = 0, lastX = 0, lastY = 0;

  void leave(uint64_t now, int toX, int toY) {
    const auto p = placement(now);
    fromX = p.x, fromY = p.y, fromAlpha = p.alpha;
    swiped = phase == Dragging;
    dirX = toX, dirY = toY;
    phase = Leaving;
    since = now;
  }
};
} // namespace hardrpg
