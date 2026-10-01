/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_KEYBOARD_H
#define SDL_KEYBOARD_H

#include "SDL_stdinc.h"
#include "SDL_scancode.h"
#include "SDL_keycode.h"

typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode sym;
    Uint16 mod;
    Uint32 unused;
} SDL_Keysym;

#define SDL_MOD_NONE     0x0000
#define SDL_MOD_LSHIFT   0x0001
#define SDL_MOD_RSHIFT   0x0002
#define SDL_MOD_LCTRL    0x0040
#define SDL_MOD_RCTRL    0x0080
#define SDL_MOD_LALT     0x0100
#define SDL_MOD_RALT     0x0200
#define SDL_MOD_LGUI     0x0400
#define SDL_MOD_RGUI     0x0800
#define SDL_MOD_NUM      0x1000
#define SDL_MOD_CAPS     0x2000
#define SDL_MOD_MODE     0x4000
#define SDL_MOD_CTRL     (SDL_MOD_LCTRL | SDL_MOD_RCTRL)
#define SDL_MOD_SHIFT    (SDL_MOD_LSHIFT | SDL_MOD_RSHIFT)
#define SDL_MOD_ALT      (SDL_MOD_LALT | SDL_MOD_RALT)
#define SDL_MOD_GUI      (SDL_MOD_LGUI | SDL_MOD_RGUI)

/* Legacy KMOD aliases (SDL1.2 compat, used by mkxp-z) */
#define KMOD_NONE     SDL_MOD_NONE
#define KMOD_LSHIFT   SDL_MOD_LSHIFT
#define KMOD_RSHIFT   SDL_MOD_RSHIFT
#define KMOD_LCTRL    SDL_MOD_LCTRL
#define KMOD_RCTRL    SDL_MOD_RCTRL
#define KMOD_LALT     SDL_MOD_LALT
#define KMOD_RALT     SDL_MOD_RALT
#define KMOD_LGUI     SDL_MOD_LGUI
#define KMOD_RGUI     SDL_MOD_RGUI
#define KMOD_NUM      SDL_MOD_NUM
#define KMOD_CAPS     SDL_MOD_CAPS
#define KMOD_MODE     SDL_MOD_MODE
#define KMOD_CTRL     SDL_MOD_CTRL
#define KMOD_SHIFT    SDL_MOD_SHIFT
#define KMOD_ALT      SDL_MOD_ALT
#define KMOD_GUI      SDL_MOD_GUI

#ifdef __cplusplus
extern "C" {
#endif

const Uint8 *SDL_GetKeyboardState(int *numkeys);
SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode);
SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key);
const char *SDL_GetScancodeName(SDL_Scancode scancode);
const char *SDL_GetKeyName(SDL_Keycode key);
SDL_Keymod SDL_GetModState(void);
int SDL_HasScreenKeyboardSupport(void);
SDL_bool SDL_IsScreenKeyboardShown(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_KEYBOARD_H */
