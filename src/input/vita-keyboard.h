#pragma once

#include "vita_controls.h"
#include <SDL_scancode.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace hardrpg {

// All coordinates are in 960x544 display pixels. The renderer and hit
// testing use the same rectangles.
struct KeyboardRect {
  int x, y, w, h;
  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// The glass keyboard: four rows of large keys over the bottom half of the
// screen. Letters, ?123 (digits and symbols) and Fn (function keys,
// arrows, editing keys) are pages of the same grid.
class GameKeyboard {
public:
  enum Action { Symbols = -1, Functions = -2, Letters = -3 };
  enum Page { LetterPage, SymbolPage, FunctionPage };
  enum Kind { Char, Word, Icon };
  struct Key {
    int code;
    const char *label; // a character, a word, or an icon name (Kind Icon)
    Kind kind;
    bool shifted;      // a symbol typed with Shift (! is Shift+1)
    KeyboardRect rect;
  };
  struct LayoutData {
    std::vector<KeyboardRect> panels;
    std::vector<Key> keys;
  };
  static constexpr int top = 268, bottom = 538;
  // The frosted backdrop is taken from here: the panel, and the key
  // preview shown above a held top-row key.
  static KeyboardRect glassArea() { return {8, 196, 944, 342}; }

  // The two buttons in the left border: the in-game menu, then the
  // keyboard. On a game that fills the width there is no menu button and
  // the keyboard button sits over the game's top left corner.
  static bool menuRect(int gameX, KeyboardRect &r) {
    if (gameX < 52)
      return false;
    const int w = std::min(64, gameX - 12), h = w * 7 / 8;
    r = {(gameX - w) / 2, 16, w, h};
    return true;
  }
  static KeyboardRect iconRect(int gameX) {
    KeyboardRect menu;
    if (!menuRect(gameX, menu))
      return {8, 8, 52, 46};
    return {menu.x, menu.y + menu.h + menu.w * 22 / 64, menu.w, menu.h};
  }
  // Generous touch areas: the whole border width, and half the gap
  // between the buttons above and below each one.
  static bool menuTouched(int gameX, int x, int y) {
    KeyboardRect menu;
    if (!menuRect(gameX, menu) || x >= gameX)
      return false;
    const auto kb = iconRect(gameX);
    return y < (menu.y + menu.h + kb.y) / 2;
  }
  static bool iconTouched(int gameX, int x, int y) {
    const auto kb = iconRect(gameX);
    KeyboardRect menu;
    if (!menuRect(gameX, menu))
      return KeyboardRect{kb.x - 8, kb.y - 8, kb.w + 16, kb.h + 16}.contains(x, y);
    const int gap = kb.y - menu.y - menu.h;
    return x < gameX && y >= kb.y - gap / 2 && y < kb.y + kb.h + gap / 2;
  }

  static const LayoutData &layout(bool split, Page page) {
    static const LayoutData all[2][3] = {
        {makeLayout(false, LetterPage), makeLayout(false, SymbolPage), makeLayout(false, FunctionPage)},
        {makeLayout(true, LetterPage), makeLayout(true, SymbolPage), makeLayout(true, FunctionPage)}};
    return all[split ? 1 : 0][page];
  }
  const LayoutData &layout() const { return layout(split, page); }

  // Where a held character key shows its larger copy: above the key,
  // where the finger does not cover it.
  static KeyboardRect previewRect(const KeyboardRect &key) {
    const int h = key.h + 8;
    return {key.x - 6, key.y - h - 6, key.w + 12, h};
  }

  bool visible() const { return shown; }
  bool capturing() const { return shown || draining; }
  bool isSplit() const { return split; }
  Page currentPage() const { return page; }
  bool shifted() const { return shift || caps; }
  bool capsLock() const { return caps; }
  bool ctrlHeld() const { return ctrl; }
  bool altHeld() const { return alt; }
  bool controlledByPad() const { return padFocus; }
  int focusIndex() const { return focus; }
  int downCode() const { return heldCode; }
  // The key drawn lit: held now, or fading for 100 ms after release.
  int litIndex() const { return litKey; }
  float litAlpha(uint64_t at) const {
    if (litKey < 0)
      return 0;
    if (heldCode)
      return 1;
    const uint64_t since = at - releasedAt;
    return since >= 100 ? 0 : 1 - since / 100.f;
  }
  // A character key held by touch: its preview is shown.
  bool previewing() const {
    return heldCode && heldByTouch && litKey >= 0 && layout().keys[litKey].kind == Char;
  }
  // The text a key shows now (letters follow Shift).
  std::string keyText(const Key &key) const {
    std::string text = key.label;
    if (key.kind == Char && !shifted() && text.size() == 1 && text[0] >= 'A' && text[0] <= 'Z')
      text[0] = char(text[0] - 'A' + 'a');
    return text;
  }
  // The keyboard opened by itself for a game asking for text.
  bool openedForText() const { return autoOpened; }
  // The menu button was tapped while the keyboard was open.
  bool takeMenuRequest() {
    const bool r = menuRequest;
    menuRequest = false;
    return r;
  }

  void open(bool forText = false) {
    shown = true;
    armed = false;
    draining = false;
    autoOpened = forText;
    page = LetterPage;
    focus = firstLetter();
  }
  void close() {
    shown = false;
    draining = true;
    autoOpened = false;
    armed = shift = caps = ctrl = alt = false;
    heldCode = 0;
    litKey = -1;
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
    now = sample.milliseconds;
    constexpr uint16_t Cross = 1u << 0, Circle = 1u << 1;
    constexpr uint16_t Square = 1u << 2, Triangle = 1u << 3;
    constexpr uint16_t Up = 1u << 6, Down = 1u << 7, Left = 1u << 8, Right = 1u << 9;
    if (edges & Circle) {
      close();
      return 0;
    }
    if (heldCode && (heldByTouch ? !down : !(sample.buttons & Cross))) {
      heldCode = 0;
      releasedAt = now;
      if (shiftedKey && !caps)
        shift = false;
      shiftedKey = false;
    }
    if (edges & Triangle) {
      split = !split;
      litKey = -1;
      focus = firstLetter();
      return 0;
    }
    if (edges & Square) {
      activate(Functions, false, -1);
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

    const auto &keys = layout().keys;
    if (tapped) {
      padFocus = false;
      const int x = int(sample.front[0].x * 960), y = int(sample.front[0].y * 544);
      if (iconTouched(gameX, x, y)) {
        close();
        return 0;
      }
      if (menuTouched(gameX, x, y)) {
        menuRequest = true;
        return 0;
      }
      // A touch in the gap between keys goes to the nearest one.
      int best = -1, bestDistance = 6;
      for (size_t i = 0; i < keys.size(); ++i) {
        const auto &r = keys[i].rect;
        const int dx = x < r.x ? r.x - x : x >= r.x + r.w ? x - r.x - r.w + 1 : 0;
        const int dy = y < r.y ? r.y - y : y >= r.y + r.h ? y - r.y - r.h + 1 : 0;
        if (dx + dy < bestDistance) {
          bestDistance = dx + dy;
          best = int(i);
        }
      }
      if (best >= 0) {
        focus = best;
        return activate(keys[best].code, true, best);
      }
    } else if (edges & Cross) {
      padFocus = true;
      return activate(keys[focus].code, false, focus);
    }
    return 0;
  }

  void setGameOffset(int offset) { gameX = offset; }

  std::array<bool, 256> heldKeys() const {
    std::array<bool, 256> keys{};
    if (shown) {
      if (heldCode > 0) keys[heldCode] = true;
      keys[SDL_SCANCODE_LSHIFT] = shift || caps || (heldCode > 0 && heldShifted);
      keys[SDL_SCANCODE_LCTRL] = ctrl;
      keys[SDL_SCANCODE_LALT] = alt;
    }
    return keys;
  }

private:
  struct KeySpec { int code; const char *label; Kind kind; float w; bool shifted; };
  bool shown = false, draining = false, armed = false, split = false;
  bool shift = false, caps = false, ctrl = false, alt = false, shiftedKey = false, heldByTouch = false;
  bool heldShifted = false, prevTouch = false, padFocus = false, autoOpened = false, menuRequest = false;
  Page page = LetterPage, beforeFunctions = LetterPage;
  uint16_t prevButtons = 0;
  uint64_t now = 0, releasedAt = 0, lastShiftTap = 0;
  int focus = 0, heldCode = 0, gameX = 0, litKey = -1;

  // A typed character's key: its scancode, and whether Shift makes it.
  static bool symbolKey(char c, int &code, bool &shifted) {
    static const char plain[] = "1234567890-=[]\\;'`,./";
    static const char upper[] = "!@#$%^&*()_+{}|:\"~<>?";
    static const int codes[] = {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
        SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS,
        SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_SEMICOLON,
        SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_GRAVE, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH};
    for (int i = 0; plain[i]; ++i) {
      if (plain[i] == c || upper[i] == c) {
        code = codes[i];
        shifted = upper[i] == c;
        return true;
      }
    }
    if (c >= 'A' && c <= 'Z') {
      code = SDL_SCANCODE_A + (c - 'A');
      shifted = false;
      return true;
    }
    return false;
  }

  static std::vector<KeySpec> chars(const char *text) {
    static char storage[128][2];
    std::vector<KeySpec> keys;
    for (const char *c = text; *c; ++c) {
      int code = 0;
      bool shifted = false;
      symbolKey(*c, code, shifted);
      char *label = storage[(unsigned char)*c & 127];
      label[0] = *c;
      label[1] = 0;
      keys.push_back({code, label, Char, 1, shifted});
    }
    return keys;
  }

  static std::array<std::vector<KeySpec>, 4> rowsFor(Page page) {
    const KeySpec esc{SDL_SCANCODE_ESCAPE, "Esc", Word, 1.25f, false};
    const KeySpec back{SDL_SCANCODE_BACKSPACE, "backspace", Icon, 1.5f, false};
    const KeySpec tab{SDL_SCANCODE_TAB, "Tab", Word, 1.5f, false};
    const KeySpec enter{SDL_SCANCODE_RETURN, "Enter", Word, 2, false};
    const KeySpec shiftKey{SDL_SCANCODE_LSHIFT, "shift", Icon, 1.75f, false};
    std::array<std::vector<KeySpec>, 4> rows;
    auto append = [](std::vector<KeySpec> &row, const std::vector<KeySpec> &middle) {
      row.insert(row.end(), middle.begin(), middle.end());
    };
    if (page == FunctionPage) {
      static const char *names[] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10"};
      rows[0].push_back(esc);
      for (int i = 0; i < 10; ++i)
        rows[0].push_back({SDL_SCANCODE_F1 + i, names[i], Word, 1, false});
      rows[0].push_back(back);
      rows[1] = {tab, {SDL_SCANCODE_F11, "F11", Word, 1, false}, {SDL_SCANCODE_F12, "F12", Word, 1, false},
                 {SDL_SCANCODE_INSERT, "Ins", Word, 1, false}, {SDL_SCANCODE_DELETE, "Del", Word, 1, false},
                 {SDL_SCANCODE_HOME, "Home", Word, 1, false}, {SDL_SCANCODE_END, "End", Word, 1, false},
                 {SDL_SCANCODE_PAGEUP, "PgUp", Word, 1, false}, {SDL_SCANCODE_PAGEDOWN, "PgDn", Word, 1, false},
                 enter};
      rows[2] = {shiftKey, {SDL_SCANCODE_LEFT, "left", Icon, 2.25f, false},
                 {SDL_SCANCODE_UP, "up", Icon, 2.25f, false}, {SDL_SCANCODE_DOWN, "down", Icon, 2.25f, false},
                 {SDL_SCANCODE_RIGHT, "right", Icon, 2.25f, false}, shiftKey};
    } else {
      const bool letters = page == LetterPage;
      rows[0].push_back(esc);
      append(rows[0], chars(letters ? "QWERTYUIOP" : "1234567890"));
      rows[0].push_back(back);
      rows[1].push_back(tab);
      append(rows[1], chars(letters ? "ASDFGHJKL" : "-=[]\\;'`/"));
      rows[1].push_back(enter);
      rows[2].push_back(shiftKey);
      append(rows[2], chars(letters ? "ZXCVBNM,." : "!@#$%^&*?"));
      rows[2].push_back(shiftKey);
    }
    rows[3] = {{page == LetterPage ? Symbols : Letters, page == LetterPage ? "?123" : "ABC", Word, 1.6f, false},
               {SDL_SCANCODE_LCTRL, "Ctrl", Word, 1.35f, false},
               {SDL_SCANCODE_LALT, "Alt", Word, 1.35f, false},
               {SDL_SCANCODE_SPACE, "Space", Word, 6.8f, false},
               {Functions, "Fn", Word, 1.35f, false}};
    return rows;
  }

  static LayoutData makeLayout(bool split, Page page) {
    const auto rows = rowsFor(page);
    constexpr int pad = 8, gap = 6;
    LayoutData result;
    if (split) {
      result.panels.push_back({8, top, 424, bottom - top});
      result.panels.push_back({528, top, 424, bottom - top});
    } else {
      result.panels.push_back({8, top, 944, bottom - top});
    }
    const float step = float(bottom - top - 2 * pad + gap) / 4;
    for (size_t row = 0; row < rows.size(); ++row) {
      auto specs = rows[row];
      // Split: a Space on each half, so either thumb reaches one.
      if (split && row == 3)
        specs = {specs[0], specs[1], {SDL_SCANCODE_SPACE, "Space", Word, 3.4f, false},
                 {SDL_SCANCODE_SPACE, "Space", Word, 3.4f, false}, specs[2], specs[4]};
      float total = 0;
      for (const auto &key : specs)
        total += key.w;
      // Split halves divide each row where its middle falls.
      size_t middle = specs.size();
      if (split) {
        float run = 0;
        for (size_t i = 0; i < specs.size(); ++i) {
          if (run + specs[i].w / 2 > total / 2) {
            middle = i;
            break;
          }
          run += specs[i].w;
        }
      }
      const int y = top + pad + int(row * step + 0.5f);
      const int h = top + pad + int((row + 1) * step + 0.5f) - gap - y;
      for (size_t side = 0; side < result.panels.size(); ++side) {
        const auto &panel = result.panels[side];
        const size_t first = side ? middle : 0, last = (side || !split) ? specs.size() : middle;
        if (first >= last)
          continue;
        float units = 0;
        for (size_t i = first; i < last; ++i)
          units += specs[i].w;
        const float unit = float(panel.w - 2 * pad - gap * int(last - first - 1)) / units;
        float x = float(panel.x + pad);
        for (size_t i = first; i < last; ++i) {
          const float w = specs[i].w * unit;
          const int left = int(x + 0.5f), right = int(x + w + 0.5f);
          result.keys.push_back({specs[i].code, specs[i].label, specs[i].kind, specs[i].shifted,
                                 {left, y, right - left, h}});
          x += w + gap;
        }
      }
    }
    return result;
  }

  // The first key of the top row that is not a word key (Q, 1 or F1's row).
  int firstLetter() const {
    const auto &keys = layout().keys;
    for (size_t i = 0; i < keys.size(); ++i)
      if (keys[i].rect.y < top + 20 && keys[i].code != SDL_SCANCODE_ESCAPE)
        return int(i);
    return 0;
  }

  void move(int dx, int dy) {
    const auto &keys = layout().keys;
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

  void turnPage(Page to) {
    page = to;
    litKey = -1;
    focus = std::min(focus, int(layout().keys.size()) - 1);
  }

  char activate(int code, bool touch, int index) {
    switch (code) {
    case Symbols: turnPage(SymbolPage); return 0;
    case Letters: turnPage(LetterPage); return 0;
    case Functions:
      if (page == FunctionPage) {
        turnPage(beforeFunctions);
      } else {
        beforeFunctions = page;
        turnPage(FunctionPage);
      }
      return 0;
    case SDL_SCANCODE_LSHIFT:
      // Tap: Shift for the next key. A second tap soon after: Caps Lock.
      if (caps)
        caps = shift = false;
      else if (shift && now - lastShiftTap < 400)
        caps = true, shift = false;
      else
        shift = !shift;
      lastShiftTap = now;
      return 0;
    case SDL_SCANCODE_LCTRL: ctrl = !ctrl; return 0;
    case SDL_SCANCODE_LALT: alt = !alt; return 0;
    default: break;
    }
    heldCode = code;
    heldByTouch = touch;
    shiftedKey = shift;
    litKey = index;
    const Key *key = index >= 0 ? &layout().keys[index] : nullptr;
    heldShifted = key && key->shifted;
    if (ctrl || alt)
      return 0;
    if (key && key->kind == Char) {
      const char c = key->label[0];
      if (c >= 'A' && c <= 'Z')
        return shifted() ? c : char(c - 'A' + 'a');
      return c;
    }
    if (code == SDL_SCANCODE_SPACE)
      return ' ';
    return 0;
  }
};
}
