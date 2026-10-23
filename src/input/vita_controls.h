#pragma once
#include "util/json5pp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace hardrpg {
// Actions use RGSS codes. Physical source order is independent of SDK masks.
static const char *const controlSources[] = {
    "Cross", "Circle", "Square", "Triangle", "Select", "Start",
    "Up",    "Down",   "Left",   "Right",    "L",      "R"};
struct ControlAction {
  int code;
  const char *name;
};
static const ControlAction controlActions[] = {
    {0, "Off"}, {2, "DOWN"},        {4, "LEFT"},        {6, "RIGHT"},
    {8, "UP"},  {11, "A / Run"},    {12, "B / Cancel"}, {13, "C / Confirm"},
    {14, "X"},  {15, "Y"},          {16, "Z"},          {17, "L"},
    {18, "R"},  {21, "SHIFT"},      {22, "CTRL"},       {23, "ALT"},
    {25, "F5"}, {26, "F6"},         {27, "F7"},         {28, "F8"},
    {29, "F9"}, {38, "Mouse left"}, {40, "Mouse right"},
    {43, "Fast forward"}};
// Not an RPG Maker button: the engine's fast forward (see
// display/fast-forward.h), which L+R also switches.
static const int controlFastForward = 43;
// A mapping is either an RPG Maker action (the codes above) or a keyboard
// key, for games that read the keyboard themselves (LISA) or whose own key
// bindings are what the player knows. A key is controlKeyBase plus its USB
// usage number, which is also its SDL scancode.
//
// controlPicks is everything the launcher's picker offers: one row of RPG
// Maker actions, then a keyboard. row, x and w place each entry, in key
// widths. Shift, Ctrl, Alt and F5-F9 are offered as the keys they are.
static const int controlKeyBase = 1000;
struct ControlPick {
  int value;
  const char *name;
  int row;
  float x, w;
};
static const ControlPick controlPicks[] = {
    // RPG Maker's moves first: a touch zone or button can walk.
    {8, "\xe2\x86\x91", 0, 0.00f, 0.70f},
    {2, "\xe2\x86\x93", 0, 0.70f, 0.70f},
    {4, "\xe2\x86\x90", 0, 1.40f, 0.70f},
    {6, "\xe2\x86\x92", 0, 2.10f, 0.70f},
    {13, "Confirm", 0, 2.80f, 1.45f},
    {12, "Cancel", 0, 4.25f, 1.40f},
    {11, "Run", 0, 5.65f, 1.00f},
    {14, "X=A", 0, 6.65f, 0.75f},
    {15, "Y=S", 0, 7.40f, 0.75f},
    {16, "Z=D", 0, 8.15f, 0.75f},
    {17, "L=Q", 0, 8.90f, 0.75f},
    {18, "R=W", 0, 9.65f, 0.75f},
    {38, "Mouse L", 0, 10.40f, 1.20f},
    {40, "Mouse R", 0, 11.60f, 1.20f},
    {43, "Fast fwd", 0, 12.80f, 1.20f},
    {0, "Off", 0, 14.00f, 1.00f},
    {1041, "Esc", 1, 0.00f, 1.50f},
    {1058, "F1", 1, 1.50f, 1.00f},
    {1059, "F2", 1, 2.50f, 1.00f},
    {1060, "F3", 1, 3.50f, 1.00f},
    {1061, "F4", 1, 4.50f, 1.00f},
    {1062, "F5", 1, 5.50f, 1.00f},
    {1063, "F6", 1, 6.50f, 1.00f},
    {1064, "F7", 1, 7.50f, 1.00f},
    {1065, "F8", 1, 8.50f, 1.00f},
    {1066, "F9", 1, 9.50f, 1.00f},
    {1067, "F10", 1, 10.50f, 1.00f},
    {1068, "F11", 1, 11.50f, 1.00f},
    {1069, "F12", 1, 12.50f, 1.00f},
    {1053, "`", 2, 0.00f, 1.00f},
    {1030, "1", 2, 1.00f, 1.00f},
    {1031, "2", 2, 2.00f, 1.00f},
    {1032, "3", 2, 3.00f, 1.00f},
    {1033, "4", 2, 4.00f, 1.00f},
    {1034, "5", 2, 5.00f, 1.00f},
    {1035, "6", 2, 6.00f, 1.00f},
    {1036, "7", 2, 7.00f, 1.00f},
    {1037, "8", 2, 8.00f, 1.00f},
    {1038, "9", 2, 9.00f, 1.00f},
    {1039, "0", 2, 10.00f, 1.00f},
    {1045, "-", 2, 11.00f, 1.00f},
    {1046, "=", 2, 12.00f, 1.00f},
    {1042, "Backspace", 2, 13.00f, 2.00f},
    {1043, "Tab", 3, 0.00f, 1.50f},
    {1020, "Q", 3, 1.50f, 1.00f},
    {1026, "W", 3, 2.50f, 1.00f},
    {1008, "E", 3, 3.50f, 1.00f},
    {1021, "R", 3, 4.50f, 1.00f},
    {1023, "T", 3, 5.50f, 1.00f},
    {1028, "Y", 3, 6.50f, 1.00f},
    {1024, "U", 3, 7.50f, 1.00f},
    {1012, "I", 3, 8.50f, 1.00f},
    {1018, "O", 3, 9.50f, 1.00f},
    {1019, "P", 3, 10.50f, 1.00f},
    {1047, "[", 3, 11.50f, 1.00f},
    {1048, "]", 3, 12.50f, 1.00f},
    {1049, "\\", 3, 13.50f, 1.50f},
    {1057, "Caps", 4, 0.00f, 1.75f},
    {1004, "A", 4, 1.75f, 1.00f},
    {1022, "S", 4, 2.75f, 1.00f},
    {1007, "D", 4, 3.75f, 1.00f},
    {1009, "F", 4, 4.75f, 1.00f},
    {1010, "G", 4, 5.75f, 1.00f},
    {1011, "H", 4, 6.75f, 1.00f},
    {1013, "J", 4, 7.75f, 1.00f},
    {1014, "K", 4, 8.75f, 1.00f},
    {1015, "L", 4, 9.75f, 1.00f},
    {1051, ";", 4, 10.75f, 1.00f},
    {1052, "'", 4, 11.75f, 1.00f},
    {1040, "Enter", 4, 12.75f, 2.25f},
    {1225, "LShift", 5, 0.00f, 2.25f},
    {1029, "Z", 5, 2.25f, 1.00f},
    {1027, "X", 5, 3.25f, 1.00f},
    {1006, "C", 5, 4.25f, 1.00f},
    {1025, "V", 5, 5.25f, 1.00f},
    {1005, "B", 5, 6.25f, 1.00f},
    {1017, "N", 5, 7.25f, 1.00f},
    {1016, "M", 5, 8.25f, 1.00f},
    {1054, ",", 5, 9.25f, 1.00f},
    {1055, ".", 5, 10.25f, 1.00f},
    {1056, "/", 5, 11.25f, 1.00f},
    {1229, "RShift", 5, 12.25f, 2.75f},
    {1224, "LCtrl", 6, 0.00f, 1.50f},
    {1226, "LAlt", 6, 1.50f, 1.50f},
    {1044, "Space", 6, 3.00f, 5.00f},
    {1230, "RAlt", 6, 8.00f, 1.50f},
    {1228, "RCtrl", 6, 9.50f, 1.50f},
    {1080, "Left", 6, 11.00f, 1.00f},
    {1081, "Down", 6, 12.00f, 1.00f},
    {1082, "Up", 6, 13.00f, 1.00f},
    {1079, "Right", 6, 14.00f, 1.00f},
    {1073, "Ins", 7, 0.00f, 1.25f},
    {1076, "Del", 7, 1.25f, 1.25f},
    {1074, "Home", 7, 2.50f, 1.25f},
    {1077, "End", 7, 3.75f, 1.25f},
    {1075, "PgUp", 7, 5.00f, 1.25f},
    {1078, "PgDn", 7, 6.25f, 1.25f},
    {1084, "Num/", 7, 7.50f, 1.25f},
    {1085, "Num*", 7, 8.75f, 1.25f},
    {1086, "Num-", 7, 10.00f, 1.25f},
    {1087, "Num+", 7, 11.25f, 1.25f},
    {1088, "NumEnter", 7, 12.50f, 1.50f},
    {1099, "Num.", 7, 14.00f, 1.00f},
    {1089, "Num1", 8, 0.00f, 1.50f},
    {1090, "Num2", 8, 1.50f, 1.50f},
    {1091, "Num3", 8, 3.00f, 1.50f},
    {1092, "Num4", 8, 4.50f, 1.50f},
    {1093, "Num5", 8, 6.00f, 1.50f},
    {1094, "Num6", 8, 7.50f, 1.50f},
    {1095, "Num7", 8, 9.00f, 1.50f},
    {1096, "Num8", 8, 10.50f, 1.50f},
    {1097, "Num9", 8, 12.00f, 1.50f},
    {1098, "Num0", 8, 13.50f, 1.50f},
};
static const int controlPickCount = sizeof(controlPicks) / sizeof(*controlPicks);
static const int controlPickRows = 9;
inline bool controlIsKey(int code) {
  return code >= controlKeyBase && code < controlKeyBase + 256;
}
// Index into controlPicks, or -1.
inline int controlPickIndex(int value) {
  for (int i = 0; i < controlPickCount; ++i)
    if (controlPicks[i].value == value)
      return i;
  return -1;
}
// Where an entry sits on the picker, in launcher pixels.
inline void controlPickRect(int index, int &x, int &y, int &w, int &h) {
  const ControlPick &k = controlPicks[index];
  x = 194 + int(k.x * 47);
  y = 146 + k.row * 34 + (k.row ? 8 : 0);
  w = int(k.w * 47) - 3;
  h = 31;
}
inline std::string controlName(int code) {
  if (controlIsKey(code)) {
    int key = controlPickIndex(code);
    return key >= 0 ? std::string("Key ") + controlPicks[key].name : "Off";
  }
  // RPG Maker's own button letters are easy to take for keyboard keys, so
  // each names the PC key that sends it.
  switch (code) {
  case 11: return "Run";
  case 12: return "Cancel";
  case 13: return "Confirm";
  case 14: return "RPG X (PC key A)";
  case 15: return "RPG Y (PC key S)";
  case 16: return "RPG Z (PC key D)";
  case 17: return "RPG L (PC key Q)";
  case 18: return "RPG R (PC key W)";
  case 2: return "Down";
  case 4: return "Left";
  case 6: return "Right";
  case 8: return "Up";
  }
  for (auto &a : controlActions)
    if (a.code == code)
      return a.name;
  return "Off";
}
inline bool controlValid(int code) {
  if (controlIsKey(code))
    return controlPickIndex(code) >= 0;
  for (auto &a : controlActions)
    if (a.code == code)
      return true;
  return false;
}
inline int cycleControl(int code, int step) {
  int count = sizeof(controlActions) / sizeof(*controlActions), at = 0;
  for (int i = 0; i < count; ++i)
    if (controlActions[i].code == code)
      at = i;
  return controlActions[(at + step + count) % count].code;
}
// Each touch surface has four zone mappings: top left, top right, bottom
// left, bottom right. Its mode decides which of them it reads:
//   front 1 two drawn corner buttons, 3 D-pad + drawn button, 4 whole
//   screen, 5 left and right halves, 6 four corners (2 is the pointer);
//   rear  1 two zones along the top edge, 3 four corners, 4 whole pad,
//   5 left and right halves (2 is the trackpad).
// The answer is a mask of zones, bit 0 top left.
inline unsigned touchZones(bool rear, int mode) {
  switch (mode) {
  case 1: return 3;
  case 3: return rear ? 15 : 2;
  case 4: return 1;
  case 5: return 3;
  case 6: return rear ? 0 : 15;
  }
  return 0;
}
static const int frontModes = 7, rearModes = 6;
struct ControlProfile {
  bool enabled = false, toggleRun = false, rightPointer = false;
  // Walk to where the front screen is tapped. Works with or without the
  // custom mappings above; see launcher/vita_tap_to_walk.rb.
  bool tapToWalk = false;
  int deadzone = 24, front = 0, rear = 0, speed = 4;
  int frontLeft = 11, frontRight = 13, rearLeft = 11, rearRight = 12;
  // rear 3 splits the rear pad into four corners: rearLeft and rearRight
  // are the top two, these the bottom two.
  int rearBottomLeft = 0, rearBottomRight = 0;
  int frontBottomLeft = 0, frontBottomRight = 0;
  int *zone(bool rear, int i) {
    int *front[] = {&frontLeft, &frontRight, &frontBottomLeft, &frontBottomRight};
    int *back[] = {&rearLeft, &rearRight, &rearBottomLeft, &rearBottomRight};
    return (rear ? back : front)[i];
  }
  int zone(bool rear, int i) const {
    return *const_cast<ControlProfile *>(this)->zone(rear, i);
  }
  std::array<int, 12> buttons{{13, 12, 11, 14, 16, 15, 8, 2, 4, 6, 17, 18}};
  json5pp::value json() const {
    using J = json5pp::value;
    J out(std::initializer_list<J::pair_type>{});
    auto &o = out.as_object();
    o["enabled"] = enabled;
    o["toggleRun"] = toggleRun;
    o["tapToWalk"] = tapToWalk;
    o["rightPointer"] = rightPointer;
    o["deadzone"] = deadzone;
    o["front"] = front;
    o["rear"] = rear;
    o["speed"] = speed;
    o["frontLeft"] = frontLeft;
    o["frontRight"] = frontRight;
    o["rearLeft"] = rearLeft;
    o["rearRight"] = rearRight;
    o["rearBottomLeft"] = rearBottomLeft;
    o["rearBottomRight"] = rearBottomRight;
    o["frontBottomLeft"] = frontBottomLeft;
    o["frontBottomRight"] = frontBottomRight;
    J a(std::initializer_list<J>{});
    for (int b : buttons)
      a.as_array().push_back(b);
    o["buttons"] = a;
    return out;
  }
  static ControlProfile parse(const json5pp::value &j) {
    ControlProfile p;
    const auto &o = j.as_object();
    auto flag = [&](const char *k, bool &v) {
      auto i = o.find(k);
      if (i != o.end())
        v = i->second.as_boolean();
    };
    auto number = [&](const char *k, int &v, int lo, int hi) {
      auto i = o.find(k);
      if (i == o.end())
        return;
      auto n = i->second.as_integer();
      if (n < lo || n > hi)
        throw std::runtime_error(std::string("Invalid control setting: ") + k);
      v = int(n);
    };
    auto action = [&](int v) {
      if (!controlValid(v))
        throw std::runtime_error("Invalid control action");
    };
    flag("enabled", p.enabled);
    flag("toggleRun", p.toggleRun);
    flag("tapToWalk", p.tapToWalk);
    flag("rightPointer", p.rightPointer);
    number("deadzone", p.deadzone, 8, 80);
    number("front", p.front, 0, frontModes - 1);
    number("rear", p.rear, 0, rearModes - 1);
    number("speed", p.speed, 1, 10);
    number("frontLeft", p.frontLeft, 0, controlKeyBase + 255);
    number("frontRight", p.frontRight, 0, controlKeyBase + 255);
    number("rearLeft", p.rearLeft, 0, controlKeyBase + 255);
    number("rearRight", p.rearRight, 0, controlKeyBase + 255);
    number("rearBottomLeft", p.rearBottomLeft, 0, controlKeyBase + 255);
    number("rearBottomRight", p.rearBottomRight, 0, controlKeyBase + 255);
    number("frontBottomLeft", p.frontBottomLeft, 0, controlKeyBase + 255);
    number("frontBottomRight", p.frontBottomRight, 0, controlKeyBase + 255);
    auto i = o.find("buttons");
    if (i != o.end()) {
      auto &a = i->second.as_array();
      if (a.size() != 12)
        throw std::runtime_error("Expected 12 button mappings");
      for (size_t n = 0; n < a.size(); ++n) {
        auto code = a[n].as_integer();
        if (code < 0 || code > controlKeyBase + 255)
          throw std::runtime_error("Invalid control action");
        p.buttons[n] = int(code);
        action(p.buttons[n]);
      }
    }
    action(p.frontLeft);
    action(p.frontRight);
    action(p.rearLeft);
    action(p.rearRight);
    action(p.rearBottomLeft);
    action(p.rearBottomRight);
    action(p.frontBottomLeft);
    action(p.frontBottomRight);
    return p;
  }
};
struct ControlContact {
  int id;
  float x, y;
};
struct ControlSample {
  uint64_t milliseconds = 0;
  uint16_t buttons = 0;
  int lx = 128, ly = 128, rx = 128, ry = 128;
  std::vector<ControlContact> front, rear;
};
struct ControlResult {
  std::array<bool, 44> actions{};
  // Keyboard keys held through a key mapping, by scancode.
  std::array<bool, 256> keys{};
  bool pointer = false;
  float x = 0.5f, y = 0.5f;
};
class ControlState {
  bool armed = false, run = false;
  uint16_t previous = 0;
  uint64_t last = 0;
  float px = 0.5f, py = 0.5f;
  int rearId = -1;
  float rearX = 0, rearY = 0;
  // Contacts starting in the grip area never become action contacts.
  std::vector<int> rearAccepted, rearKnown;
  // The zone each finger is in, kept while it sits in the band between two.
  std::vector<std::pair<int, int>> zoneOf[2];
  // Which zone a contact presses under a layout of mask, or -1. Halves and
  // corners leave a band in the middle where a finger keeps its last zone,
  // so one resting on a line cannot flicker between two.
  int zoneAt(int pad, unsigned mask, const ControlContact &c,
             std::vector<std::pair<int, int>> &next) {
    if (!mask || c.x < 0 || c.x > 1 || c.y < 0 || c.y > 1)
      return -1;
    int last = -1;
    for (auto &z : zoneOf[pad])
      if (z.first == c.id)
        last = z.second;
    int zone = -1;
    if (mask == 1 || mask == 2)
      zone = mask == 1 ? 0 : 1;
    else {
      int col = c.x < .45f ? 0 : c.x > .55f ? 1 : -1;
      int row = mask == 3 ? 0 : c.y < .45f ? 0 : c.y > .55f ? 1 : -1;
      if (col >= 0 && row >= 0)
        zone = row * 2 + col;
      else if (last >= 0)
        zone = last;
      else
        zone = (row < 0 ? (c.y < .5f ? 0 : 1) : row) * 2 +
               (col < 0 ? (c.x < .5f ? 0 : 1) : col);
    }
    next.push_back({c.id, zone});
    return zone;
  }
  static float axis(int value, int deadzone) {
    int d = value - 128;
    if (std::abs(d) <= deadzone)
      return 0;
    return float(d - (d < 0 ? -deadzone : deadzone)) / float(128 - deadzone);
  }

public:
  void reset(unsigned reason = 1) {
    (void)reason;
    armed = false;
    run = false;
    previous = 0;
    last = 0;
    rearId = -1;
    rearAccepted.clear();
    rearKnown.clear();
    zoneOf[0].clear();
    zoneOf[1].clear();
  }
  ControlResult update(const ControlProfile &p, const ControlSample &s) {
    ControlResult out;
    auto finish = [&]() { return out; };
    out.x = px;
    out.y = py;
    if (!p.enabled) {
      reset();
      return finish();
    }
    if (last && (s.milliseconds < last || s.milliseconds - last > 250))
      reset(s.milliseconds < last ? 8 : 4);
    float dt =
        last ? std::min(0.05f, float(s.milliseconds - last) / 1000.f) : 0;
    last = s.milliseconds;
    bool touchActive = false;
    for (auto &c : s.front) {
      bool inside = c.x >= 0 && c.x <= 1 && c.y >= 0 && c.y <= 1;
      if (inside &&
          (p.front == 2 || p.front >= 4 ||
           (p.front == 1 && c.y >= .8f && (c.x < .25f || c.x > .75f)) ||
           (p.front == 3 &&
            ((c.x < .36f && c.y > .55f) || (c.x > .75f && c.y >= .8f)))))
        touchActive = true;
    }
    if (p.rear == 1)
      for (auto &c : s.rear)
        if (c.y >= 0 && c.y < .35f && (c.x < .35f || c.x > .65f)) {
          touchActive = true;
        }
    bool neutral = !s.buttons && !touchActive && !axis(s.lx, p.deadzone) &&
                   !axis(s.ly, p.deadzone) &&
                   (!p.rightPointer ||
                    (!axis(s.rx, p.deadzone) && !axis(s.ry, p.deadzone)));
    if (!armed) {
      rearKnown.clear();
      for (auto &c : s.rear)
        rearKnown.push_back(c.id);
      if (neutral)
        armed = true;
      previous = s.buttons;
      return finish();
    }
    out.pointer = p.front == 2 || p.rear == 2 || p.rightPointer;
    auto press = [&](int action) {
      if (action > 0 && action < 44)
        out.actions[action] = true;
      else if (controlIsKey(action))
        out.keys[action - controlKeyBase] = true;
    };
    for (int i = 0; i < 12; ++i)
      if (s.buttons & (1u << i)) {
        if ((i != 2 || !p.toggleRun) && (i != 10 || p.rear != 2))
          press(p.buttons[i]);
      }
    if (p.toggleRun && (s.buttons & 4) && !(previous & 4))
      run = !run;
    if (run)
      press(p.buttons[2]);
    float lx = axis(s.lx, p.deadzone), ly = axis(s.ly, p.deadzone);
    if (lx < 0)
      press(4);
    if (lx > 0)
      press(6);
    if (ly < 0)
      press(8);
    if (ly > 0)
      press(2);
    // The standard run key exposes both RGSS A and SHIFT.
    if (out.actions[11])
      press(21);
    if (p.front == 1)
      for (auto &c : s.front) {
        if (c.y >= 0.8f && c.y <= 1 && c.x >= 0 && c.x <= 1) {
          if (c.x < 0.25f)
            press(p.frontLeft);
          if (c.x > 0.75f)
            press(p.frontRight);
        }
      }
    if (p.front == 3)
      for (auto &c : s.front) {
        if (c.x >= 0 && c.x < 0.36f && c.y > 0.55f && c.y <= 1) {
          float dx = (c.x - 0.18f) / 0.18f, dy = (c.y - 0.775f) / 0.225f;
          if (std::max(std::abs(dx), std::abs(dy)) > 0.2f)
            press(std::abs(dx) > std::abs(dy) ? (dx < 0 ? 4 : 6)
                                              : (dy < 0 ? 8 : 2));
        } else if (c.x > 0.75f && c.x <= 1 && c.y >= 0.8f && c.y <= 1)
          press(p.frontRight);
      }
    if (p.front >= 4) {
      // Invisible zones over the whole screen; nothing is drawn.
      std::vector<std::pair<int, int>> next;
      for (auto &c : s.front) {
        int zone = zoneAt(0, touchZones(false, p.front), c, next);
        if (zone >= 0)
          press(p.zone(false, zone));
      }
      zoneOf[0] = next;
    }
    if (p.front == 2 && !s.front.empty()) {
      auto &c = s.front[0];
      if (c.x >= 0 && c.x <= 1 && c.y >= 0 && c.y <= 1) {
        px = c.x;
        py = c.y;
        press(38);
        out.pointer = true;
      }
    }
    std::vector<int> now;
    std::vector<std::pair<int, int>> rearZones;
    for (auto &c : s.rear) {
      now.push_back(c.id);
      // A finger counts only if it landed after the profile armed, so the
      // ones resting on the pad while holding the console never press
      // anything. The two-zone mode also wants it to land in the top strip.
      if (std::find(rearKnown.begin(), rearKnown.end(), c.id) ==
              rearKnown.end() &&
          c.y < (p.rear == 1 ? 0.35f : 1.0001f) && c.x >= 0 && c.x <= 1 && c.y >= 0)
        rearAccepted.push_back(c.id);
      if ((p.rear == 3 || p.rear >= 4) &&
          std::find(rearAccepted.begin(), rearAccepted.end(), c.id) !=
              rearAccepted.end()) {
        int zone = zoneAt(1, touchZones(true, p.rear), c, rearZones);
        if (zone >= 0)
          press(p.zone(true, zone));
      }
      if (p.rear == 1 && c.y >= 0 && c.y < 0.35f &&
          std::find(rearAccepted.begin(), rearAccepted.end(), c.id) !=
              rearAccepted.end()) {
        if (c.x < 0.35f)
          press(p.rearLeft);
        if (c.x > 0.65f)
          press(p.rearRight);
      }
    }
    rearKnown = now;
    zoneOf[1] = rearZones;
    rearAccepted.erase(std::remove_if(rearAccepted.begin(), rearAccepted.end(),
                                      [&](int id) {
                                        return std::find(now.begin(), now.end(),
                                                         id) == now.end();
                                      }),
                       rearAccepted.end());
    if (p.rear == 2) {
      const ControlContact *contact = nullptr;
      // A physical L hold clutches the trackpad and rejects resting fingers.
      if (s.buttons & (1u << 10))
        for (auto &c : s.rear)
          if (c.x >= 0.2f && c.x <= 0.8f && c.y >= 0.15f && c.y <= 0.85f &&
              (!contact || c.id == rearId))
            contact = &c;
      if (contact) {
        if (rearId == contact->id) {
          px += (contact->x - rearX) * p.speed / 4.f;
          py += (contact->y - rearY) * p.speed / 4.f;
        }
        rearId = contact->id;
        rearX = contact->x;
        rearY = contact->y;
      } else
        rearId = -1;
      out.pointer = true;
    }
    if (p.rightPointer) {
      px += axis(s.rx, p.deadzone) * dt * p.speed * 0.2f;
      py += axis(s.ry, p.deadzone) * dt * p.speed * 0.2f;
      out.pointer = true;
    }
    if (out.actions[11])
      press(21);
    px = std::max(0.f, std::min(px, 1.f));
    py = std::max(0.f, std::min(py, 1.f));
    out.x = px;
    out.y = py;
    previous = s.buttons;
    return finish();
  }
};
} // namespace hardrpg
