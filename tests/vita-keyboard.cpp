#include "input/vita-keyboard.h"
#include <cassert>
#include <cstdio>

using hardrpg::ControlSample;
using hardrpg::GameKeyboard;

static ControlSample touch(const GameKeyboard &keyboard, int code) {
    const auto &keys = GameKeyboard::layout(keyboard.isSplit(), keyboard.hasFunctions()).keys;
    for (const auto &key : keys) {
        if (key.code == code) {
            ControlSample sample{};
            const auto &r = key.rect;
            sample.front.push_back({0, (r.x + r.w / 2.f) / 960, (r.y + r.h / 2.f) / 544});
            return sample;
        }
    }
    assert(false && "key missing");
    return {};
}

int main() {
    for (bool split : {false, true}) {
        for (bool fn : {false, true}) {
            const auto &layout = GameKeyboard::layout(split, fn);
            assert(layout.panels.size() == (split ? 2u : 1u));
            for (size_t i = 0; i < layout.keys.size(); ++i) {
                const auto &r = layout.keys[i].rect;
                assert(r.w >= 30 && r.h >= 36);
                bool inside = false;
                for (const auto &panel : layout.panels)
                    inside |= panel.contains(r.x, r.y) && panel.contains(r.x + r.w - 1, r.y + r.h - 1);
                assert(inside);
                for (size_t j = i + 1; j < layout.keys.size(); ++j) {
                    const auto &other = layout.keys[j].rect;
                    assert(r.x >= other.x + other.w || other.x >= r.x + r.w ||
                           r.y >= other.y + other.h || other.y >= r.y + r.h);
                }
            }
        }
    }

    GameKeyboard keyboard;
    keyboard.open();
    assert(keyboard.step(touch(keyboard, SDL_SCANCODE_A)) == 0);
    assert(!keyboard.heldKeys()[SDL_SCANCODE_A]); // opening contact
    keyboard.step({});
    keyboard.step(touch(keyboard, SDL_SCANCODE_LSHIFT));
    keyboard.step({});
    assert(keyboard.step(touch(keyboard, SDL_SCANCODE_A)) == 'A');
    assert(keyboard.heldKeys()[SDL_SCANCODE_A] && keyboard.heldKeys()[SDL_SCANCODE_LSHIFT]);
    assert(keyboard.step(touch(keyboard, SDL_SCANCODE_A)) == 0); // same contact
    keyboard.step({});
    assert(!keyboard.heldKeys()[SDL_SCANCODE_A] && !keyboard.shifted());
    keyboard.step(touch(keyboard, GameKeyboard::Layout));
    keyboard.step({});
    assert(keyboard.isSplit());
    assert(keyboard.step(touch(keyboard, SDL_SCANCODE_Z)) == 'z');
    keyboard.step({});
    keyboard.step(touch(keyboard, GameKeyboard::Functions));
    keyboard.step({});
    assert(keyboard.hasFunctions());
    keyboard.step(touch(keyboard, SDL_SCANCODE_F5));
    assert(keyboard.heldKeys()[SDL_SCANCODE_F5]);
    keyboard.step({});
    ControlSample pad{};
    pad.buttons = 1u << 8;
    keyboard.step(pad);
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
    assert(GameKeyboard::iconRect(0).contains(20, 20));
    assert(GameKeyboard::iconRect(120).contains(60, 70));
    std::puts("PASS: docked and split hit targets, touch and pad, modifier and release handling");
}
