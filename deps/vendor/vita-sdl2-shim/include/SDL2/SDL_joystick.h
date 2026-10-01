/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_JOYSTICK_H
#define SDL_JOYSTICK_H

#include "SDL_stdinc.h"

#define SDL_JOYSTICK_MAX_DEVICES 4

typedef struct _SDL_Joystick SDL_Joystick;
typedef Sint32 SDL_JoystickID;

#undef SDL_HAT_CENTERED
#define SDL_HAT_CENTERED    0x00
#define SDL_HAT_UP          0x01
#define SDL_HAT_RIGHT       0x02
#define SDL_HAT_DOWN        0x04
#define SDL_HAT_LEFT        0x08
#define SDL_HAT_RIGHTUP     (SDL_HAT_RIGHT | SDL_HAT_UP)
#define SDL_HAT_RIGHTDOWN   (SDL_HAT_RIGHT | SDL_HAT_DOWN)
#define SDL_HAT_LEFTUP      (SDL_HAT_LEFT | SDL_HAT_UP)
#define SDL_HAT_LEFTDOWN    (SDL_HAT_LEFT | SDL_HAT_DOWN)

/* Joystick power levels */
#define SDL_JOYSTICK_POWER_UNKNOWN   (-1)
#define SDL_JOYSTICK_POWER_EMPTY     0
#define SDL_JOYSTICK_POWER_LOW       1
#define SDL_JOYSTICK_POWER_MEDIUM    2
#define SDL_JOYSTICK_POWER_FULL      3
#define SDL_JOYSTICK_POWER_WIRED     4
#define SDL_JOYSTICK_POWER_MAX       5

#ifdef __cplusplus
extern "C" {
#endif

int SDL_NumJoysticks(void);
const char *SDL_JoystickNameForIndex(int device_index);
SDL_Joystick *SDL_JoystickOpen(int device_index);
SDL_bool SDL_JoystickGetAttached(SDL_Joystick *joystick);
const char *SDL_JoystickName(SDL_Joystick *joystick);
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *joystick);
int SDL_JoystickNumAxes(SDL_Joystick *joystick);
int SDL_JoystickNumBalls(SDL_Joystick *joystick);
int SDL_JoystickNumHats(SDL_Joystick *joystick);
int SDL_JoystickNumButtons(SDL_Joystick *joystick);
void SDL_JoystickUpdate(void);
int SDL_JoystickGetButton(SDL_Joystick *joystick, int button);
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis);
Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat);
int SDL_JoystickGetBall(SDL_Joystick *joystick, int ball, int *dx, int *dy);
void SDL_JoystickClose(SDL_Joystick *joystick);
int SDL_JoystickEventState(int state);
int SDL_JoystickCurrentPowerLevel(SDL_Joystick *joystick);

#ifdef __cplusplus
}
#endif

#endif /* SDL_JOYSTICK_H */
