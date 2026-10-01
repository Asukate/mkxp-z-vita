/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_GAMECONTROLLER_H
#define SDL_GAMECONTROLLER_H

#include "SDL_stdinc.h"
#include "SDL_rwops.h"
#include "SDL_joystick.h"

typedef struct _SDL_GameController SDL_GameController;

typedef enum {
    SDL_CONTROLLER_BINDTYPE_NONE,
    SDL_CONTROLLER_BINDTYPE_BUTTON,
    SDL_CONTROLLER_BINDTYPE_AXIS,
    SDL_CONTROLLER_BINDTYPE_HAT
} SDL_GameControllerBindType;

typedef enum {
    SDL_CONTROLLER_AXIS_INVALID = -1,
    SDL_CONTROLLER_AXIS_LEFTX,
    SDL_CONTROLLER_AXIS_LEFTY,
    SDL_CONTROLLER_AXIS_RIGHTX,
    SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT,
    SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    SDL_CONTROLLER_AXIS_MAX
} SDL_GameControllerAxis;

typedef enum {
    SDL_CONTROLLER_BUTTON_INVALID = -1,
    SDL_CONTROLLER_BUTTON_A,
    SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_GUIDE,
    SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_MISC1,
    SDL_CONTROLLER_BUTTON_PADDLE1,
    SDL_CONTROLLER_BUTTON_PADDLE2,
    SDL_CONTROLLER_BUTTON_PADDLE3,
    SDL_CONTROLLER_BUTTON_PADDLE4,
    SDL_CONTROLLER_BUTTON_TOUCHPAD,
    SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;

#ifdef __cplusplus
extern "C" {
#endif

int SDL_GameControllerAddMapping(const char *mappingString);
int SDL_GameControllerNumMappings(void);
char *SDL_GameControllerMappingForGUID(void *guid);
char *SDL_GameControllerMapping(SDL_GameController *gamecontroller);
char *SDL_GameControllerMappingForDeviceIndex(int joystick_index);
SDL_bool SDL_IsGameController(int joystick_index);
const char *SDL_GameControllerNameForIndex(int joystick_index);
const char *SDL_GameControllerName(SDL_GameController *gamecontroller);
SDL_GameController *SDL_GameControllerOpen(int joystick_index);
SDL_GameController *SDL_GameControllerFromInstanceID(SDL_JoystickID joyid);
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *gamecontroller);
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gamecontroller);
int SDL_GameControllerEventState(int state);
void SDL_GameControllerUpdate(void);
Sint16 SDL_GameControllerGetAxis(SDL_GameController *gamecontroller, SDL_GameControllerAxis axis);
Uint8 SDL_GameControllerGetButton(SDL_GameController *gamecontroller, SDL_GameControllerButton button);
void SDL_GameControllerClose(SDL_GameController *gamecontroller);
int SDL_GameControllerAddMappingsFromRW(SDL_RWops *rw, int freesrc);
int SDL_GameControllerAddMapping(const char *mapping);
const char *SDL_GameControllerGetStringForAxis(SDL_GameControllerAxis axis);
SDL_GameControllerAxis SDL_GameControllerGetAxisFromString(const char *str);
const char *SDL_GameControllerGetStringForButton(SDL_GameControllerButton button);
SDL_GameControllerButton SDL_GameControllerGetButtonFromString(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* SDL_GAMECONTROLLER_H */
