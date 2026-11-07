#pragma once

#include "vita_controls.h"
#include <SDL_scancode.h>
#include <array>
#include <limits>

namespace hardrpg {

// All coordinates are in 960x544 display pixels. The renderer and hit
// testing use the same rectangles.
struct KeyboardRect {
  int x, y, w, h;
  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

class GameKeyboard {
public:
  enum Action { Layout = -1, Functions = -2, Close = -3 };
  struct Key {
    int code;
    const char *label;
    KeyboardRect rect;
  };
  struct LayoutData {
    std::vector<KeyboardRect> panels;
    std::vector<Key> keys;
  };
  static constexpr int top = 252;

  static KeyboardRect iconRect(int gameX) {
    return {gameX >= 52 ? (gameX - 35) / 2 : 14, gameX >= 52 ? 60 : 14, 35, 30};
  }

  static const LayoutData &layout(bool split, bool functions) {
    static const auto docked = makeLayout(false, false);
    static const auto dockedFn = makeLayout(false, true);
    static const auto splitKeys = makeLayout(true, false);
    static const auto splitFn = makeLayout(true, true);
    return split ? (functions ? splitFn : splitKeys) : (functions ? dockedFn : docked);
  }

  bool visible() const { return shown; }
  bool capturing() const { return shown || draining; }
  bool isSplit() const { return split; }
  bool hasFunctions() const { return functions; }
  bool shifted() const { return shift; }
  bool controlledByPad() const { return padFocus; }
  int focusIndex() const { return focus; }
  int downCode() const { return heldCode; }

  void open() {
    shown = true;
    armed = false;
    draining = false;
    focus = firstLetter();
  }
  void close() {
    shown = false;
    draining = true;
    armed = shift = ctrl = alt = false;
    heldCode = 0;
    shiftedKey = false;
  }

  // Returns a newly entered printable character for Input.gets clients.
  // Key-polling clients use heldKeys, including the modifier transitions.
  char step(const ControlSample &sample) {
    const bool down = !sample.front.empty();
    if (!shown) {
      draining = draining && (down || sample.buttons);
      return 0;
    }
    if (!armed) {
      armed = !down && !sample.buttons;
      if (armed) {
        prevButtons = 0;
        prevTouch = false;
      }
      return 0;
    }

    const uint16_t edges = sample.buttons & ~prevButtons;
    const bool tapped = down && !prevTouch;
    prevButtons = sample.buttons;
    prevTouch = down;
    constexpr uint16_t Cross = 1u << 0, Circle = 1u << 1;
    constexpr uint16_t Square = 1u << 2, Triangle = 1u << 3;
    constexpr uint16_t Up = 1u << 6, Down = 1u << 7, Left = 1u << 8, Right = 1u << 9;
    if (edges & Circle) {
      close();
      return 0;
    }
    if (heldCode && (heldByTouch ? !down : !(sample.buttons & Cross))) {
      heldCode = 0;
      if (shiftedKey)
        shift = false;
      shiftedKey = false;
    }
    if (edges & (Triangle | Square)) {
      activate(edges & Triangle ? Layout : Functions, false);
      return 0;
    }
    if (edges & (Up | Down | Left | Right)) {
      padFocus = true;
      if (edges & Up) move(0, -1);
      else if (edges & Down) move(0, 1);
      else move(edges & Left ? -1 : 1, 0);
    }
    if (heldCode)
      return 0;

    const auto &keys = layout(split, functions).keys;
    if (tapped) {
      padFocus = false;
      const int x = int(sample.front[0].x * 960), y = int(sample.front[0].y * 544);
      if (iconRect(gameX).contains(x, y)) {
        close();
        return 0;
      }
      for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].rect.contains(x, y)) {
          focus = int(i);
          return activate(keys[i].code, true);
        }
      }
    } else if (edges & Cross) {
      padFocus = true;
      return activate(keys[focus].code, false);
    }
    return 0;
  }

  void setGameOffset(int offset) { gameX = offset; }

  std::array<bool, 256> heldKeys() const {
    std::array<bool, 256> keys{};
    if (shown) {
      if (heldCode > 0) keys[heldCode] = true;
      keys[SDL_SCANCODE_LSHIFT] = shift;
      keys[SDL_SCANCODE_LCTRL] = ctrl;
      keys[SDL_SCANCODE_LALT] = alt;
    }
    return keys;
  }

private:
  struct KeySpec { int code; const char *label; float x, w; };
  bool shown = false, draining = false, armed = false, split = false, functions = false;
  bool shift = false, ctrl = false, alt = false, shiftedKey = false, heldByTouch = false;
  bool prevTouch = false, padFocus = false;
  uint16_t prevButtons = 0;
  int focus = 0, heldCode = 0, gameX = 0;

  static LayoutData makeLayout(bool split, bool functions) {
    std::array<std::vector<KeySpec>, 6> rows;
    for (const auto &pick : controlPicks) {
      if (pick.row < 2 || pick.row > 5)
        continue;
      KeySpec key{pick.value - controlKeyBase, pick.name, pick.x, pick.w};
      if (key.code == SDL_SCANCODE_CAPSLOCK)
        key = {SDL_SCANCODE_ESCAPE, "Esc", key.x, key.w};
      else if (key.code == SDL_SCANCODE_LSHIFT)
        key.label = "Shift";
      else if (key.code == SDL_SCANCODE_RSHIFT)
        key = {Functions, "Fn", key.x, key.w};
      rows[pick.row - 1].push_back(key);
    }
    if (functions)
      rows[0] = {{SDL_SCANCODE_LCTRL, "Ctrl", 0, 1.5f}, {SDL_SCANCODE_LALT, "Alt", 1.5f, 1.5f},
                 {SDL_SCANCODE_F1, "F1", 3, 1}, {SDL_SCANCODE_F2, "F2", 4, 1},
                 {SDL_SCANCODE_F3, "F3", 5, 1}, {SDL_SCANCODE_F4, "F4", 6, 1},
                 {SDL_SCANCODE_F5, "F5", 7, 1}, {SDL_SCANCODE_F6, "F6", 8, 1},
                 {SDL_SCANCODE_F7, "F7", 9, 1}, {SDL_SCANCODE_F8, "F8", 10, 1},
                 {SDL_SCANCODE_F9, "F9", 11, 1}, {SDL_SCANCODE_F10, "F10", 12, 1},
                 {SDL_SCANCODE_F11, "F11", 13, 1}, {SDL_SCANCODE_F12, "F12", 14, 1}};
    rows[5] = {{Layout, split ? "Dock" : "Split", 0, 2}, {SDL_SCANCODE_SPACE, "Space", 2, 6},
               {SDL_SCANCODE_LEFT, "Left", 8, 1.25f}, {SDL_SCANCODE_DOWN, "Down", 9.25f, 1.25f},
               {SDL_SCANCODE_UP, "Up", 10.5f, 1.25f}, {SDL_SCANCODE_RIGHT, "Right", 11.75f, 1.25f},
               {Close, "Hide", 13, 2}};

    LayoutData result;
    const int panelY = top + (functions ? 0 : 45);
    result.panels.push_back({split ? 18 : 64, panelY, split ? 426 : 832, 532 - panelY});
    if (split)
      result.panels.push_back({516, panelY, 426, 532 - panelY});
    for (size_t row = 0; row < rows.size(); ++row) {
      for (size_t side = 0; side < result.panels.size(); ++side) {
        const auto &panel = result.panels[side];
        const auto &specs = rows[row];
        auto middle = split ? std::find_if(specs.begin(), specs.end(), [](const KeySpec &key) {
          return key.x + key.w / 2 > 7.5f;
        }) : specs.end();
        auto first = side ? middle : specs.begin();
        auto last = side ? specs.end() : middle;
        if (first == last)
          continue;
        const float unit = float(panel.w - 16) / ((last - 1)->x + (last - 1)->w - first->x);
        for (auto key = first; key != last; ++key) {
          const int x = panel.x + 8 + int((key->x - first->x) * unit);
          const int right = panel.x + 8 + int((key->x + key->w - first->x) * unit);
          result.keys.push_back({key->code, key->label, {x, top + 8 + int(row) * 45, right - x - 4, 40}});
        }
      }
    }
    return result;
  }

  int firstLetter() const {
    const auto &keys = layout(split, functions).keys;
    return int(std::find_if(keys.begin(), keys.end(), [](const Key &key) {
      return key.code == SDL_SCANCODE_Q;
    }) - keys.begin());
  }

  void move(int dx, int dy) {
    const auto &keys = layout(split, functions).keys;
    const auto &from = keys[focus].rect;
    const int center = from.x + from.w / 2;
    int best = std::numeric_limits<int>::max(), next = focus;
    for (size_t i = 0; i < keys.size(); ++i) {
      const auto &to = keys[i].rect;
      const int x = to.x + to.w / 2 - center, y = to.y - from.y;
      if ((dx && (!y && x * dx > 0)) || (dy && y * dy > 0)) {
        const int distance = std::abs(y) * 32 + std::abs(x);
        if (distance < best) {
          best = distance;
          next = int(i);
        }
      }
    }
    focus = next;
  }

  char activate(int code, bool touch) {
    switch (code) {
    case Layout: split = !split; focus = firstLetter(); return 0;
    case Functions:
      functions = !functions;
      if (!functions) ctrl = alt = false;
      focus = firstLetter();
      return 0;
    case Close: close(); return 0;
    case SDL_SCANCODE_LSHIFT: shift = !shift; return 0;
    case SDL_SCANCODE_LCTRL: ctrl = !ctrl; return 0;
    case SDL_SCANCODE_LALT: alt = !alt; return 0;
    default: break;
    }
    heldCode = code;
    heldByTouch = touch;
    shiftedKey = shift;
    if (ctrl || alt)
      return 0;
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z)
      return char((shift ? 'A' : 'a') + code - SDL_SCANCODE_A);
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_0)
      return (shift ? "!@#$%^&*()" : "1234567890")[code - SDL_SCANCODE_1];
    if (code == SDL_SCANCODE_SPACE)
      return ' ';
    static const struct { int key; char plain, shifted; } punctuation[] = {
      {SDL_SCANCODE_MINUS, '-', '_'}, {SDL_SCANCODE_EQUALS, '=', '+'},
      {SDL_SCANCODE_LEFTBRACKET, '[', '{'}, {SDL_SCANCODE_RIGHTBRACKET, ']', '}'},
      {SDL_SCANCODE_BACKSLASH, '\\', '|'}, {SDL_SCANCODE_SEMICOLON, ';', ':'},
      {SDL_SCANCODE_APOSTROPHE, '\'', '"'}, {SDL_SCANCODE_GRAVE, '`', '~'},
      {SDL_SCANCODE_COMMA, ',', '<'}, {SDL_SCANCODE_PERIOD, '.', '>'}, {SDL_SCANCODE_SLASH, '/', '?'}
    };
    for (const auto &symbol : punctuation)
      if (symbol.key == code)
        return shift ? symbol.shifted : symbol.plain;
    return 0;
  }
};
}
