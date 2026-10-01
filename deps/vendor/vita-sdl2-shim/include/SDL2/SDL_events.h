/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_EVENTS_H
#define SDL_EVENTS_H

#include "SDL_stdinc.h"
#include "SDL_keyboard.h"
#include "SDL_joystick.h"
#include "SDL_gamecontroller.h"

/* Event type constants */
#define SDL_FIRSTEVENT 0
#define SDL_QUIT 0x100
#define SDL_APP_TERMINATING 0x101
#define SDL_APP_LOWMEMORY 0x102
#define SDL_APP_WILLENTERBACKGROUND 0x103
#define SDL_APP_DIDENTERBACKGROUND 0x104
#define SDL_APP_WILLENTERFOREGROUND 0x105
#define SDL_APP_DIDENTERFOREGROUND 0x106
#define SDL_LOCALECHANGED 0x107
#define SDL_DISPLAYEVENT 0x150
#define SDL_WINDOWEVENT 0x200
#define SDL_WINDOWEVENT_SHOWN 1
#define SDL_WINDOWEVENT_HIDDEN 2
#define SDL_WINDOWEVENT_EXPOSED 3
#define SDL_WINDOWEVENT_MOVED 4
#define SDL_WINDOWEVENT_RESIZED 5
#define SDL_WINDOWEVENT_SIZE_CHANGED 6
#define SDL_WINDOWEVENT_MINIMIZED 7
#define SDL_WINDOWEVENT_MAXIMIZED 8
#define SDL_WINDOWEVENT_RESTORED 9
#define SDL_WINDOWEVENT_ENTER 10
#define SDL_WINDOWEVENT_LEAVE 11
#define SDL_WINDOWEVENT_FOCUS_GAINED 12
#define SDL_WINDOWEVENT_FOCUS_LOST 13
#define SDL_WINDOWEVENT_CLOSE 14
#define SDL_WINDOWEVENT_TAKE_FOCUS 15
#define SDL_WINDOWEVENT_HIT_TEST 16
#define SDL_WINDOWEVENT_ICCPROF_CHANGED 17
#define SDL_WINDOWEVENT_DISPLAY_CHANGED 18
#define SDL_SYSWMEVENT 0x201
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SDL_TEXTEDITING 0x302
#define SDL_TEXTINPUT 0x303
#define SDL_KEYMAPCHANGED 0x304
#define SDL_TEXTEDITING_EXT 0x305
#define SDL_MOUSEMOTION 0x400
#define SDL_MOUSEBUTTONDOWN 0x401
#define SDL_MOUSEBUTTONUP 0x402
#define SDL_MOUSEWHEEL 0x403
#define SDL_JOYAXISMOTION 0x600
#define SDL_JOYBALLMOTION 0x601
#define SDL_JOYHATMOTION 0x602
#define SDL_JOYBUTTONDOWN 0x603
#define SDL_JOYBUTTONUP 0x604
#define SDL_JOYDEVICEADDED 0x605
#define SDL_JOYDEVICEREMOVED 0x606
#define SDL_CONTROLLERAXISMOTION 0x650
#define SDL_CONTROLLERBUTTONDOWN 0x651
#define SDL_CONTROLLERBUTTONUP 0x652
#define SDL_CONTROLLERDEVICEADDED 0x653
#define SDL_CONTROLLERDEVICEREMOVED 0x654
#define SDL_CONTROLLERDEVICEREMAPPED 0x655
#define SDL_CONTROLLERTOUCHPADDOWN 0x656
#define SDL_CONTROLLERTOUCHPADMOTION 0x657
#define SDL_CONTROLLERTOUCHPADUP 0x658
#define SDL_FINGERDOWN 0x700
#define SDL_FINGERUP 0x701
#define SDL_FINGERMOTION 0x702
#define SDL_DOLLARGESTURE 0x800
#define SDL_DOLLARRECORD 0x801
#define SDL_MULTIGESTURE 0x802
#define SDL_CLIPBOARDUPDATE 0x900
#define SDL_DROPFILE 0x1000
#define SDL_DROPTEXT 0x1001
#define SDL_DROPBEGIN 0x1002
#define SDL_DROPCOMPLETE 0x1003
#define SDL_AUDIODEVICEADDED 0x1100
#define SDL_AUDIODEVICEREMOVED 0x1101
#define SDL_SENSORUPDATE 0x1200
#define SDL_RENDER_TARGETS_RESET 0x2000
#define SDL_RENDER_DEVICE_RESET 0x2001
#define SDL_POLLSENTINEL 0x7F00
#define SDL_USEREVENT 0x8000
#define SDL_LASTEVENT 0xFFFF

#define SDL_IGNORE 0
#define SDL_ENABLE 1
#define SDL_QUERY -1
#define SDL_RELEASED 0
#define SDL_PRESSED 1

/* Named event struct types */
typedef struct SDL_CommonEvent {
    Uint32 type;
    Uint32 timestamp;
} SDL_CommonEvent;

typedef struct SDL_WindowEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Sint32 data1;
    Sint32 data2;
    Uint8 event;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
} SDL_WindowEvent;

typedef struct SDL_KeyboardEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 state;
    Uint8 repeat;
    Uint8 padding2;
    Uint8 padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseMotionEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint32 state;
    Sint32 x;
    Sint32 y;
    Sint32 xrel;
    Sint32 yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 clicks;
    Uint8 padding1;
    Sint32 x;
    Sint32 y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseWheelEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Sint32 x;
    Sint32 y;
    Uint32 direction;
} SDL_MouseWheelEvent;

typedef struct SDL_JoyAxisEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
    Uint8 axis;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_JoyAxisEvent;

typedef struct SDL_JoyButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1;
    Uint8 padding2;
} SDL_JoyButtonEvent;

typedef struct SDL_ControllerAxisEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
    Uint8 axis;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_ControllerAxisEvent;

typedef struct SDL_ControllerButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1;
    Uint8 padding2;
} SDL_ControllerButtonEvent;

typedef struct SDL_ControllerDeviceEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
} SDL_ControllerDeviceEvent;

typedef struct SDL_QuitEvent {
    Uint32 type;
    Uint32 timestamp;
} SDL_QuitEvent;

typedef struct SDL_UserEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Sint32 code;
    void *data1;
    void *data2;
} SDL_UserEvent;

typedef struct SDL_AudioDeviceEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 which;
    Uint8 state;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
} SDL_AudioDeviceEvent;

typedef struct SDL_TouchFingerEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 touchId;
    Sint32 fingerId;
    float x;
    float y;
    float dx;
    float dy;
    float pressure;
} SDL_TouchFingerEvent;

typedef struct SDL_TextEditingEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    char text[32];
    Sint32 start;
    Sint32 length;
} SDL_TextEditingEvent;

typedef struct SDL_TextInputEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    char text[32];
} SDL_TextInputEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_CommonEvent common;
    SDL_WindowEvent window;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyButtonEvent jbutton;
    SDL_ControllerAxisEvent caxis;
    SDL_ControllerButtonEvent cbutton;
    SDL_ControllerDeviceEvent cdevice;
    SDL_QuitEvent quit;
    SDL_AudioDeviceEvent adevice;
    SDL_TouchFingerEvent tfinger;
    SDL_TextEditingEvent edit;
    SDL_TextInputEvent text;
    SDL_UserEvent user;
} SDL_Event;

#define SDL_QUERY -1
#define SDL_IGNORE 0
#define SDL_DISABLE 0
#define SDL_ENABLE 1

typedef int SDL_EventFilter(void *userdata, SDL_Event *event);

#ifdef __cplusplus
extern "C" {
#endif

/* Text input */
void SDL_StartTextInput(void);
void SDL_StopTextInput(void);
void SDL_SetTextInputRect(const SDL_Rect *rect);
SDL_bool SDL_IsTextInputActive(void);

int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEvent(SDL_Event *event);
int SDL_WaitEventTimeout(SDL_Event *event, int timeout);
int SDL_PushEvent(SDL_Event *event);
void SDL_PumpEvents(void);
int SDL_PeepEvents(SDL_Event *events, int numevents, int action, Uint32 minType, Uint32 maxType);
SDL_bool SDL_HasEvent(Uint32 type);
SDL_bool SDL_HasEvents(Uint32 minType, Uint32 maxType);
void SDL_FlushEvent(Uint32 type);
void SDL_FlushEvents(Uint32 minType, Uint32 maxType);
int SDL_EventState(Uint32 type, int state);
Uint32 SDL_RegisterEvents(int numevents);
void SDL_SetEventFilter(SDL_EventFilter *filter, void *userdata);
SDL_bool SDL_GetEventFilter(SDL_EventFilter **filter, void **userdata);

#ifdef __cplusplus
}
#endif

#endif /* SDL_EVENTS_H */
