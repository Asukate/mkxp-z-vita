#include "input/vita-keyboard.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using hardrpg::ControlSample;
using hardrpg::GameKeyboard;

static uint64_t now = 1000;

static ControlSample at(int x, int y) {
    ControlSample sample{};
    sample.milliseconds = now;
    hardrpg::ControlContact contact;
    contact.id = 0;
    contact.x = (x + 0.5f) / 960;
    contact.y = (y + 0.5f) / 544;
    sample.front.push_back(contact);
    return sample;
}

static ControlSample idle() {
    ControlSample sample{};
    sample.milliseconds = (now += 50);
    return sample;
}

static ControlSample touch(const GameKeyboard &keyboard, int code, const char *label = nullptr) {
    for (const auto &key : keyboard.layout().keys) {
        if (key.code == code && (!label || !std::strcmp(key.label, label))) {
            const auto &r = key.rect;
            return at(r.x + r.w / 2, r.y + r.h / 2);
        }
    }
    assert(false && "key missing");
    return {};
}

static std::string text(const GameKeyboard &keyboard, int code) {
    for (const auto &key : keyboard.layout().keys)
        if (key.code == code)
            return keyboard.keyText(key);
    return "";
}

static char tap(GameKeyboard &keyboard, int code, const char *label = nullptr) {
    const char c = keyboard.step(touch(keyboard, code, label));
    keyboard.step(idle());
    return c;
}

int main() {
    using Page = GameKeyboard::Page;
    for (bool split : {false, true}) {
        for (Page page : {GameKeyboard::LetterPage, GameKeyboard::SymbolPage, GameKeyboard::FunctionPage}) {
            const auto &layout = GameKeyboard::layout(split, page);
            assert(layout.panels.size() == (split ? 2u : 1u));
            for (size_t i = 0; i < layout.keys.size(); ++i) {
                const auto &r = layout.keys[i].rect;
                // One finger: at least about 6.4 x 6.4 mm on the Vita screen.
                assert(r.w >= 56 && r.h >= 56);
                bool inside = false;
                for (const auto &panel : layout.panels)
                    inside |= panel.contains(r.x, r.y) && panel.contains(r.x + r.w - 1, r.y + r.h - 1);
                assert(inside);
                for (size_t j = i + 1; j < layout.keys.size(); ++j) {
                    const auto &other = layout.keys[j].rect;
                    assert(r.x >= other.x + other.w || other.x >= r.x + r.w ||
                           r.y >= other.y + other.h || other.y >= r.y + r.h);
                }
                const auto preview = GameKeyboard::previewRect(r);
                const auto glass = GameKeyboard::glassArea();
                assert(preview.y >= glass.y && preview.x >= 0 && preview.x + preview.w <= 960);
            }
        }
    }

    GameKeyboard keyboard;
    keyboard.open();
    assert(keyboard.step(touch(keyboard, SDL_SCANCODE_A)) == 0);
    assert(!keyboard.heldKeys()[SDL_SCANCODE_A]); // the opening contact
    keyboard.step(idle());

    // Lowercase, one-shot Shift, then Caps Lock with a double tap.
    assert(text(keyboard, SDL_SCANCODE_Q) == "q");
    assert(keyboard.step(touch(keyboard, SDL_SCANCODE_A)) == 'a');
    assert(keyboard.previewing());
    keyboard.step(idle());
    assert(!keyboard.previewing() && keyboard.litAlpha(now) > 0 && keyboard.litAlpha(now + 100) == 0);
    tap(keyboard, SDL_SCANCODE_LSHIFT);
    assert(keyboard.shifted() && text(keyboard, SDL_SCANCODE_Q) == "Q");
    assert(keyboard.step(touch(keyboard, SDL_SCANCODE_A)) == 'A');
    assert(keyboard.heldKeys()[SDL_SCANCODE_A] && keyboard.heldKeys()[SDL_SCANCODE_LSHIFT]);
    assert(keyboard.step(touch(keyboard, SDL_SCANCODE_A)) == 0); // same contact
    keyboard.step(idle());
    assert(!keyboard.heldKeys()[SDL_SCANCODE_A] && !keyboard.shifted());
    tap(keyboard, SDL_SCANCODE_LSHIFT);
    tap(keyboard, SDL_SCANCODE_LSHIFT);
    assert(keyboard.capsLock());
    assert(tap(keyboard, SDL_SCANCODE_B) == 'B' && tap(keyboard, SDL_SCANCODE_C) == 'C');
    tap(keyboard, SDL_SCANCODE_LSHIFT);
    assert(!keyboard.shifted() && tap(keyboard, SDL_SCANCODE_D) == 'd');

    // ?123: digits plain, symbols hold Shift with their key.
    tap(keyboard, GameKeyboard::Symbols);
    assert(keyboard.currentPage() == GameKeyboard::SymbolPage);
    assert(tap(keyboard, SDL_SCANCODE_7) == '7');
    assert(keyboard.step(touch(keyboard, SDL_SCANCODE_1, "!")) == '!');
    assert(keyboard.heldKeys()[SDL_SCANCODE_1] && keyboard.heldKeys()[SDL_SCANCODE_LSHIFT]);
    keyboard.step(idle());
    assert(!keyboard.heldKeys()[SDL_SCANCODE_LSHIFT]);
    tap(keyboard, GameKeyboard::Letters);
    assert(keyboard.currentPage() == GameKeyboard::LetterPage);

    // Fn and back to the page it came from.
    tap(keyboard, GameKeyboard::Functions);
    assert(keyboard.currentPage() == GameKeyboard::FunctionPage);
    keyboard.step(touch(keyboard, SDL_SCANCODE_F5));
    assert(keyboard.heldKeys()[SDL_SCANCODE_F5]);
    keyboard.step(idle());
    keyboard.step(touch(keyboard, SDL_SCANCODE_UP));
    assert(keyboard.heldKeys()[SDL_SCANCODE_UP] && !keyboard.previewing());
    keyboard.step(idle());
    tap(keyboard, GameKeyboard::Functions);
    assert(keyboard.currentPage() == GameKeyboard::LetterPage);

    // A touch in the gap between two keys still types.
    {
        const GameKeyboard::Key *f = nullptr, *g = nullptr;
        for (const auto &key : keyboard.layout().keys) {
            if (key.code == SDL_SCANCODE_F) f = &key;
            if (key.code == SDL_SCANCODE_G) g = &key;
        }
        assert(f && g && g->rect.x - (f->rect.x + f->rect.w) >= 4);
        assert(keyboard.step(at(f->rect.x + f->rect.w + 1, f->rect.y + 20)) == 'f');
        keyboard.step(idle());
    }

    // Triangle splits, Square turns Fn on and off.
    ControlSample pad{};
    pad.buttons = 1u << 3;
    keyboard.step(pad);
    keyboard.step(idle());
    assert(keyboard.isSplit());
    assert(tap(keyboard, SDL_SCANCODE_Z) == 'z');
    pad.buttons = 1u << 2;
    keyboard.step(pad);
    keyboard.step(idle());
    assert(keyboard.currentPage() == GameKeyboard::FunctionPage);
    keyboard.step(pad);
    keyboard.step(idle());
    assert(keyboard.currentPage() == GameKeyboard::LetterPage);

    // D-pad and Cross, then Circle closes; input stays withheld until let go.
    for (int i = 0; i < 4; ++i) {
        pad.buttons = 1u << 6; // Up to the top row: every key there is held
        keyboard.step(pad);
        keyboard.step(idle());
    }
    assert(keyboard.controlledByPad());
    pad.buttons = 1u << 0;
    keyboard.step(pad);
    assert(keyboard.downCode() > 0);
    pad.buttons = 1u << 1;
    keyboard.step(pad);
    assert(!keyboard.visible() && keyboard.capturing() && keyboard.downCode() == 0);
    keyboard.step(pad);
    assert(keyboard.capturing());
    keyboard.step({});
    assert(!keyboard.capturing());

    // Border buttons: large, apart, and the menu one hands over to the menu.
    for (int gameX : {120, 80, 52}) {
        hardrpg::KeyboardRect menu;
        assert(GameKeyboard::menuRect(gameX, menu));
        const auto kb = GameKeyboard::iconRect(gameX);
        assert(menu.x >= 0 && menu.x + menu.w <= gameX && kb.y - menu.y - menu.h >= 10);
        assert(GameKeyboard::menuTouched(gameX, menu.x + menu.w / 2, menu.y + menu.h / 2));
        assert(GameKeyboard::iconTouched(gameX, kb.x + kb.w / 2, kb.y + kb.h / 2));
        assert(!GameKeyboard::menuTouched(gameX, kb.x + kb.w / 2, kb.y + 2));
        assert(!GameKeyboard::iconTouched(gameX, menu.x + menu.w / 2, menu.y + menu.h - 2));
    }
    assert(GameKeyboard::iconRect(120).w == 64 && GameKeyboard::iconTouched(0, 20, 20));
    keyboard.setGameOffset(120);
    keyboard.open();
    keyboard.step(idle());
    keyboard.step(at(60, 40));
    assert(keyboard.takeMenuRequest() && !keyboard.takeMenuRequest());
    keyboard.step(idle());
    keyboard.step(at(60, 120));
    assert(!keyboard.visible());
    std::puts("PASS: pages, Shift and Caps, symbols, gaps, split, pad, border buttons");
}
