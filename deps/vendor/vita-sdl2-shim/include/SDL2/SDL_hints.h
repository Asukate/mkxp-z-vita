/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_HINTS_H
#define SDL_HINTS_H

#include "SDL_stdinc.h"

#define SDL_HINT_FRAMEBUFFER_ACCELERATION "SDL_FRAMEBUFFER_ACCELERATION"
#define SDL_HINT_RENDER_VSYNC "SDL_RENDER_VSYNC"
#define SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR "SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR"
#define SDL_HINT_VIDEO_FOREIGN_WINDOW "SDL_VIDEO_FOREIGN_WINDOW"
#define SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4 "SDL_WINDOWS_NO_CLOSE_ON_ALT_F4"
#define SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"
#define SDL_HINT_ACCELEROMETER_AS_JOYSTICK "SDL_ACCELEROMETER_AS_JOYSTICK"
#define SDL_HINT_GAMECONTROLLERCONFIG "SDL_GAMECONTROLLERCONFIG"
#define SDL_HINT_MOUSE_RELATIVE_MODE_WARP "SDL_MOUSE_RELATIVE_MODE_WARP"
#define SDL_HINT_OPENGL_ES_DRIVER "SDL_OPENGL_ES_DRIVER"
#define SDL_HINT_IME_SHOW_UI "SDL_IME_SHOW_UI"
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS"
#define SDL_HINT_GRAB_KEYBOARD "SDL_GRAB_KEYBOARD"

#ifdef __cplusplus
extern "C" {
#endif

SDL_bool SDL_SetHint(const char *name, const char *value);
const char *SDL_GetHint(const char *name);
SDL_bool SDL_SetHintWithPriority(const char *name, const char *value, int priority);
void SDL_ClearHints(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_HINTS_H */
