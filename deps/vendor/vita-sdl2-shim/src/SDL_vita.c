/* HardRPG - SDL2 Vita shim core implementation
 * Copyright (C) 2026 Asukate
 *
 * This file is part of HardRPG, free software under the GNU
 * General Public License v3. See LICENSES/GPL-3.0.txt at the repository root. */
/**
 * SDL2 Vita Shim — Core Implementation
 * Implements SDL2 API subset wrapping Vita native APIs:
 *   - vitaGL (graphics/GLES2)
 *   - SceCtrl (input)
 *   - SceAudio (audio)
 *   - sceKernel (threading/timing)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Keep the GLES types visible without making the SDL core archive require a
 * strong link against vitaGL.  Non-GL SDL clients must remain linkable. */
#include <GLES2/gl2.h>

/* Keep the SDL GLES ABI in control here.  Including vitaGL.h alongside the
 * SDL header exposes two slightly different legacy prototypes, so only the
 * three VitaGL lifecycle/loader entry points are declared explicitly. Weak
 * references keep SDL_Init/SDL_CreateWindow usable without the GL archive;
 * SDL_GL_CreateContext reports the missing provider when GL is requested. */
#define SDL_VITA_WEAK __attribute__((weak))
extern GLboolean vglInit(int legacy_pool_size) SDL_VITA_WEAK;
extern void vglSwapBuffers(GLboolean has_commondialog) SDL_VITA_WEAK;
extern void *vglGetProcAddress(const char *name) SDL_VITA_WEAK;
#undef SDL_VITA_WEAK

/* Vita SDK headers */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/audioout.h>
#include <psp2/types.h>
#include <psp2/gxm.h>
#include <psp2/display.h>
#include <psp2/sysmodule.h>

/* SDL2 shim headers */
#include "SDL.h"
#include "SDL_video.h"
#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_joystick.h"
#include "SDL_gamecontroller.h"
#include "SDL_audio.h"
#include "SDL_thread.h"
#include "SDL_mutex.h"
#include "SDL_rwops.h"
#include "SDL_hints.h"
#include "SDL_cpuinfo.h"
#include "SDL_power.h"

/* ======================================================
 * Internal State
 * ====================================================== */

static struct {
    Uint32 init_flags;
    int active;
    
    /* Window state */
    SDL_Window *main_window;
    int window_w;
    int window_h;
    Uint32 window_flags;
    char window_title[256];
    
    /* GL state */
    SDL_GLContext current_context;
    int gl_red_size;
    int gl_green_size;
    int gl_blue_size;
    int gl_alpha_size;
    int gl_depth_size;
    int gl_stencil_size;
    int gl_double_buffer;
    int gl_multisample_buffers;
    int gl_multisample_samples;
    int gl_context_major;
    int gl_context_minor;
    int gl_context_profile;
    int gl_swap_interval;
    
    /* Input state */
    SceCtrlData pad;
    Uint8 keyboard_state[512];
    Uint16 mod_state;
    int joystick_count;
    SDL_bool joystick_attached[SDL_JOYSTICK_MAX_DEVICES];
    
    /* Audio state */
    int audio_initialized;
    int audio_port;
    int audio_paused;
    int audio_channels;
    int audio_samples;
    
    /* Error string buffer */
    char error_str[256];
    
    /* Timers */
    int timer_count;
} sdl_state;

static int vita_gl_initialized;

/* ======================================================
 * Error Handling
 * ====================================================== */

int SDL_SetError(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(sdl_state.error_str, sizeof(sdl_state.error_str), fmt, args);
    va_end(args);
    return -1;
}

const char *SDL_GetError(void) {
    return sdl_state.error_str;
}

void SDL_ClearError(void) {
    sdl_state.error_str[0] = '\0';
}

/* ======================================================
 * Logging
 * ====================================================== */

void SDL_Log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void SDL_LogVerbose(int category, const char *fmt, ...) { (void)category; (void)fmt; }
void SDL_LogDebug(int category, const char *fmt, ...) { (void)category; (void)fmt; }
void SDL_LogInfo(int category, const char *fmt, ...) { (void)category; (void)fmt; }
void SDL_LogWarn(int category, const char *fmt, ...) { (void)category; (void)fmt; }

void SDL_LogError(int category, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void SDL_LogCritical(int category, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void SDL_LogMessage(int category, SDL_LogPriority priority, const char *fmt, ...) {
    (void)category;
    if (priority >= SDL_LOG_PRIORITY_WARN) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
    }
}

void SDL_LogSetPriority(int category, SDL_LogPriority priority) {
    (void)category; (void)priority;
}

/* ======================================================
 * Platform
 * ====================================================== */

const char *SDL_GetPlatform(void) {
    return "Vita";
}

/* ======================================================
 * CPU Info
 * ====================================================== */

int SDL_GetCPUCount(void) {
    return 4; /* Vita has 4 Cortex-A9 cores */
}

int SDL_GetSystemRAM(void) {
    return 512; /* Vita has 512 MiB shared system/GPU memory */
}

SDL_PowerState SDL_GetPowerInfo(int *seconds, int *percent) {
    if (seconds)
        *seconds = -1;
    if (percent)
        *percent = -1;
    return SDL_POWERSTATE_UNKNOWN;
}

int SDL_GetCPUCacheLineSize(void) {
    return 32;
}

SDL_bool SDL_HasNEON(void) {
    return SDL_TRUE; /* Cortex-A9 has NEON */
}

SDL_bool SDL_HasRDTSC(void) { return SDL_FALSE; }
SDL_bool SDL_HasAltiVec(void) { return SDL_FALSE; }
SDL_bool SDL_HasMMX(void) { return SDL_FALSE; }
SDL_bool SDL_Has3DNow(void) { return SDL_FALSE; }
SDL_bool SDL_HasSSE(void) { return SDL_FALSE; }
SDL_bool SDL_HasSSE2(void) { return SDL_FALSE; }
SDL_bool SDL_HasSSE3(void) { return SDL_FALSE; }
SDL_bool SDL_HasSSE41(void) { return SDL_FALSE; }
SDL_bool SDL_HasSSE42(void) { return SDL_FALSE; }
SDL_bool SDL_HasAVX(void) { return SDL_FALSE; }
SDL_bool SDL_HasAVX2(void) { return SDL_FALSE; }
SDL_bool SDL_HasAVX512F(void) { return SDL_FALSE; }

/* ======================================================
 * Endian
 * ====================================================== */

Uint16 SDL_Swap16(Uint16 x) { return (x << 8) | (x >> 8); }
Uint32 SDL_Swap32(Uint32 x) {
    return (x << 24) | ((x << 8) & 0x00FF0000) | ((x >> 8) & 0x0000FF00) | (x >> 24);
}
Uint64 SDL_Swap64(Uint64 x) {
    Uint32 lo = SDL_Swap32((Uint32)(x & 0xFFFFFFFF));
    Uint32 hi = SDL_Swap32((Uint32)(x >> 32));
    return ((Uint64)lo << 32) | hi;
}

/* ======================================================
 * Version
 * ====================================================== */

void SDL_GetVersion(SDL_version *ver) {
    ver->major = SDL_MAJOR_VERSION;
    ver->minor = SDL_MINOR_VERSION;
    ver->patch = SDL_PATCHLEVEL;
}

const char *SDL_GetRevision(void) {
    return "vita-shim-1.0";
}

int SDL_GetRevisionNumber(void) {
    return 1;
}

/* ======================================================
 * Hints
 * ====================================================== */

SDL_bool SDL_SetHint(const char *name, const char *value) {
    (void)name; (void)value;
    return SDL_TRUE;
}

const char *SDL_GetHint(const char *name) {
    (void)name;
    return NULL;
}

SDL_bool SDL_SetHintWithPriority(const char *name, const char *value, int priority) {
    (void)name; (void)value; (void)priority;
    return SDL_TRUE;
}

void SDL_ClearHints(void) {}

/* ======================================================
 * Init / Quit
 * ====================================================== */

int SDL_Init(Uint32 flags) {
    if (sdl_state.active) {
        sdl_state.init_flags |= flags;
        return 0;
    }
    
    memset(&sdl_state, 0, sizeof(sdl_state));
    
    /* Default GL attributes */
    sdl_state.gl_red_size = 5;
    sdl_state.gl_green_size = 5;
    sdl_state.gl_blue_size = 5;
    sdl_state.gl_alpha_size = 0;
    sdl_state.gl_depth_size = 16;
    sdl_state.gl_stencil_size = 0;
    sdl_state.gl_double_buffer = 1;
    sdl_state.gl_context_major = 2;
    sdl_state.gl_context_minor = 0;
    sdl_state.gl_context_profile = SDL_GL_CONTEXT_PROFILE_ES;
    sdl_state.gl_swap_interval = 1;
    
    sdl_state.init_flags = flags;
    sdl_state.active = 1;
    sdl_state.window_w = 960;
    sdl_state.window_h = 544;
    
    /* Initialize Ctrl */
    if (flags & SDL_INIT_GAMECONTROLLER || flags & SDL_INIT_JOYSTICK || flags & SDL_INIT_EVENTS) {
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    }
    
    SDL_Log("SDL Vita Shim initialized (flags: 0x%08x)", flags);
    return 0;
}

int SDL_InitSubSystem(Uint32 flags) {
    sdl_state.init_flags |= flags;
    return 0;
}

void SDL_Quit(void) {
    if (sdl_state.audio_initialized) {
        sceAudioOutReleasePort(sdl_state.audio_port);
    }
    sdl_state.active = 0;
    sdl_state.init_flags = 0;
}

void SDL_QuitSubSystem(Uint32 flags) {
    sdl_state.init_flags &= ~flags;
}

Uint32 SDL_WasInit(Uint32 flags) {
    if (flags == 0) return sdl_state.init_flags;
    return sdl_state.init_flags & flags;
}

/* ======================================================
 * Timer
 * ====================================================== */

Uint32 SDL_GetTicks(void) {
    return (Uint32)(sceKernelGetProcessTimeWide() / 1000);
}

Uint64 SDL_GetPerformanceCounter(void) {
    return sceKernelGetProcessTimeWide();
}

Uint64 SDL_GetPerformanceFrequency(void) {
    return 1000000; /* microseconds */
}

void SDL_Delay(Uint32 ms) {
    sceKernelDelayThread(ms * 1000);
}

int SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param) {
    (void)interval; (void)callback; (void)param;
    /* Stub */
    return 0;
}

SDL_bool SDL_RemoveTimer(int id) {
    (void)id;
    return SDL_TRUE;
}

/* ======================================================
 * Window & GL Context
 * ====================================================== */

int SDL_GetNumVideoDisplays(void) { return 1; }

const char *SDL_GetDisplayName(int displayIndex) {
    (void)displayIndex;
    return "Vita LCD";
}

int SDL_GetDisplayBounds(int displayIndex, SDL_Rect *rect) {
    (void)displayIndex;
    rect->x = 0;
    rect->y = 0;
    rect->w = 960;
    rect->h = 544;
    return 0;
}

int SDL_GetDisplayDPI(int displayIndex, float *ddpi, float *hdpi, float *vdpi) {
    (void)displayIndex;
    /* ~220 DPI for Vita OLED */
    if (ddpi) *ddpi = 220.0f;
    if (hdpi) *hdpi = 220.0f;
    if (vdpi) *vdpi = 220.0f;
    return 0;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
    (void)x; (void)y;
    
    if (sdl_state.main_window) {
        SDL_SetError("Window already created");
        return NULL;
    }
    
    sdl_state.main_window = (SDL_Window*)1; /* non-null sentinel */
    sdl_state.window_w = (w > 0) ? w : 960;
    sdl_state.window_h = (h > 0) ? h : 544;
    sdl_state.window_flags = flags;
    
    if (title) {
        strncpy(sdl_state.window_title, title, sizeof(sdl_state.window_title) - 1);
    }
    
    SDL_Log("Window created: %dx%d (%s)", sdl_state.window_w, sdl_state.window_h, title ? title : "untitled");
    return sdl_state.main_window;
}

void SDL_DestroyWindow(SDL_Window *window) {
    (void)window;
    if (sdl_state.current_context) {
        /* TODO: vitaGL context cleanup */
        sdl_state.current_context = NULL;
    }
    sdl_state.main_window = NULL;
}

void SDL_SetWindowTitle(SDL_Window *window, const char *title) {
    (void)window;
    if (title) {
        strncpy(sdl_state.window_title, title, sizeof(sdl_state.window_title) - 1);
    }
}

const char *SDL_GetWindowTitle(SDL_Window *window) {
    (void)window;
    return sdl_state.window_title;
}

void SDL_SetWindowSize(SDL_Window *window, int w, int h) {
    (void)window;
    sdl_state.window_w = w;
    sdl_state.window_h = h;
}

void SDL_GetWindowSize(SDL_Window *window, int *w, int *h) {
    (void)window;
    if (w) *w = sdl_state.window_w;
    if (h) *h = sdl_state.window_h;
}

Uint32 SDL_GetWindowFlags(SDL_Window *window) {
    (void)window;
    return sdl_state.window_flags;
}

void SDL_ShowWindow(SDL_Window *window) {
    (void)window;
    sdl_state.window_flags |= SDL_WINDOW_SHOWN;
}

void SDL_HideWindow(SDL_Window *window) {
    (void)window;
    sdl_state.window_flags &= ~SDL_WINDOW_SHOWN;
}

void SDL_RaiseWindow(SDL_Window *window) { (void)window; }

int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags) {
    (void)window;
    sdl_state.window_flags &= ~(SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    sdl_state.window_flags |= flags;
    return 0;
}

/* GL Functions */

int SDL_GL_LoadLibrary(const char *path) {
    (void)path;
    return 0; /* GLES is built-in via vitaGL */
}

void *SDL_GL_GetProcAddress(const char *proc) {
    if (!proc)
        return NULL;

    /* vitaGL owns both the core GLES entrypoints and extensions. */
    return vglGetProcAddress ? vglGetProcAddress(proc) : NULL;
}

int SDL_GL_SetAttribute(int attr, int value) {
    switch (attr) {
        case SDL_GL_RED_SIZE:     sdl_state.gl_red_size = value; break;
        case SDL_GL_GREEN_SIZE:   sdl_state.gl_green_size = value; break;
        case SDL_GL_BLUE_SIZE:    sdl_state.gl_blue_size = value; break;
        case SDL_GL_ALPHA_SIZE:   sdl_state.gl_alpha_size = value; break;
        case SDL_GL_DEPTH_SIZE:   sdl_state.gl_depth_size = value; break;
        case SDL_GL_STENCIL_SIZE: sdl_state.gl_stencil_size = value; break;
        case SDL_GL_DOUBLEBUFFER: sdl_state.gl_double_buffer = value; break;
        case SDL_GL_MULTISAMPLEBUFFERS: sdl_state.gl_multisample_buffers = value; break;
        case SDL_GL_MULTISAMPLESAMPLES: sdl_state.gl_multisample_samples = value; break;
        case SDL_GL_CONTEXT_MAJOR_VERSION: sdl_state.gl_context_major = value; break;
        case SDL_GL_CONTEXT_MINOR_VERSION: sdl_state.gl_context_minor = value; break;
        case SDL_GL_CONTEXT_PROFILE_MASK:  sdl_state.gl_context_profile = value; break;
        case SDL_GL_BUFFER_SIZE:
        case SDL_GL_ACCUM_RED_SIZE:
        case SDL_GL_ACCUM_GREEN_SIZE:
        case SDL_GL_ACCUM_BLUE_SIZE:
        case SDL_GL_ACCUM_ALPHA_SIZE:
        case SDL_GL_STEREO:
        case SDL_GL_CONTEXT_EGL:
        case SDL_GL_CONTEXT_FLAGS:
        case SDL_GL_SHARE_WITH_CURRENT_CONTEXT:
        case SDL_GL_ACCELERATED_VISUAL:
        case SDL_GL_RETAINED_BACKING:
        case SDL_GL_FRAMEBUFFER_SRGB_CAPABLE:
        case SDL_GL_CONTEXT_RELEASE_BEHAVIOR:
        case SDL_GL_CONTEXT_RESET_NOTIFICATION:
        case SDL_GL_CONTEXT_NO_ERROR:
        case SDL_GL_FLOATBUFFERS:
            /* Silently accept */
            break;
        default:
            return SDL_SetError("Unknown GL attribute: %d", attr);
    }
    return 0;
}

int SDL_GL_GetAttribute(int attr, int *value) {
    if (!value) return -1;
    switch (attr) {
        case SDL_GL_RED_SIZE:     *value = sdl_state.gl_red_size; break;
        case SDL_GL_GREEN_SIZE:   *value = sdl_state.gl_green_size; break;
        case SDL_GL_BLUE_SIZE:    *value = sdl_state.gl_blue_size; break;
        case SDL_GL_ALPHA_SIZE:   *value = sdl_state.gl_alpha_size; break;
        case SDL_GL_DEPTH_SIZE:   *value = sdl_state.gl_depth_size; break;
        case SDL_GL_STENCIL_SIZE: *value = sdl_state.gl_stencil_size; break;
        case SDL_GL_DOUBLEBUFFER: *value = sdl_state.gl_double_buffer; break;
        case SDL_GL_CONTEXT_MAJOR_VERSION: *value = sdl_state.gl_context_major; break;
        case SDL_GL_CONTEXT_MINOR_VERSION: *value = sdl_state.gl_context_minor; break;
        case SDL_GL_CONTEXT_PROFILE_MASK:  *value = sdl_state.gl_context_profile; break;
        default: *value = 0; break;
    }
    return 0;
}

SDL_GLContext SDL_GL_CreateContext(SDL_Window *window) {
    (void)window;
    if (!sdl_state.current_context) {
        SDL_Log("Creating Vita GLES context");
        if (!vita_gl_initialized) {
            if (!vglInit || !vglSwapBuffers || !vglGetProcAddress) {
                SDL_SetError("vitaGL is not linked");
                return NULL;
            }
            /*
             * vitaGL returns whether it had to fall back from the requested
             * display resolution.  A false return therefore means that the
             * requested size was accepted; it is not an initialization error.
             */
            GLboolean resolution_fallback = vglInit(8 * 1024 * 1024);
            SDL_Log("vitaGL initialized (resolution fallback: %d)",
                    (int)resolution_fallback);
            vita_gl_initialized = 1;
        }
        sdl_state.current_context = (SDL_GLContext)0x1;
    }
    return sdl_state.current_context;
}

void SDL_GL_DeleteContext(SDL_GLContext context) {
    if (context == sdl_state.current_context) {
        sdl_state.current_context = NULL;
    }
}

int SDL_GL_MakeCurrent(SDL_Window *window, SDL_GLContext context) {
    (void)window;
    sdl_state.current_context = context;
    return 0;
}

SDL_Window *SDL_GL_GetCurrentWindow(void) {
    return sdl_state.main_window;
}

SDL_GLContext SDL_GL_GetCurrentContext(void) {
    return sdl_state.current_context;
}

int SDL_GL_SetSwapInterval(int interval) {
    sdl_state.gl_swap_interval = interval;
    return 0;
}

int SDL_GL_GetSwapInterval(void) {
    return sdl_state.gl_swap_interval;
}

void SDL_GL_SwapWindow(SDL_Window *window) {
    (void)window;
    if (vita_gl_initialized)
        vglSwapBuffers(GL_FALSE);
}

Uint32 SDL_GetWindowID(SDL_Window *window) {
    (void)window;
    return 1;
}

SDL_Surface *SDL_GetWindowSurface(SDL_Window *window) {
    (void)window;
    return NULL;
}

int SDL_UpdateWindowSurface(SDL_Window *window) {
    (void)window;
    return 0;
}

/* ======================================================
 * Keyboard State
 * ====================================================== */

const Uint8 *SDL_GetKeyboardState(int *numkeys) {
    if (numkeys) *numkeys = sizeof(sdl_state.keyboard_state);
    return sdl_state.keyboard_state;
}

SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode) {
    /* Simple mapping: use SDL_SCANCODE_TO_KEYCODE */
    if (scancode <= 0x39) {
        return scancode + SDLK_SCANCODE_MASK;
    }
    return SDLK_UNKNOWN;
}

SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key) {
    if (key & SDLK_SCANCODE_MASK) {
        return (SDL_Scancode)(key & ~SDLK_SCANCODE_MASK);
    }
    if (key >= 'a' && key <= 'z') return SDL_SCANCODE_A + (key - 'a');
    if (key >= '0' && key <= '9') return SDL_SCANCODE_0 + (key - '0');
    return SDL_SCANCODE_UNKNOWN;
}

const char *SDL_GetScancodeName(SDL_Scancode scancode) {
    (void)scancode;
    return "?";
}

const char *SDL_GetKeyName(SDL_Keycode key) {
    (void)key;
    return "?";
}

SDL_Keymod SDL_GetModState(void) {
    return sdl_state.mod_state;
}

int SDL_HasScreenKeyboardSupport(void) { return 0; }
SDL_bool SDL_IsScreenKeyboardShown(void) { return SDL_FALSE; }

/* ======================================================
 * Joystick / GameController
 * ====================================================== */

int SDL_NumJoysticks(void) {
    return (sdl_state.init_flags & (SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER)) ? 1 : 0;
}

const char *SDL_JoystickNameForIndex(int device_index) { (void)device_index; return "Vita Controller"; }
SDL_Joystick *SDL_JoystickOpen(int device_index) {
    (void)device_index;
    if (device_index >= SDL_JOYSTICK_MAX_DEVICES) return NULL;
    sdl_state.joystick_attached[device_index] = SDL_TRUE;
    sdl_state.joystick_count++;
    return (SDL_Joystick*)(uintptr_t)(device_index + 1);
}

SDL_bool SDL_JoystickGetAttached(SDL_Joystick *joystick) {
    int idx = ((uintptr_t)joystick) - 1;
    if (idx < 0 || idx >= SDL_JOYSTICK_MAX_DEVICES) return SDL_FALSE;
    return sdl_state.joystick_attached[idx];
}

const char *SDL_JoystickName(SDL_Joystick *joystick) { (void)joystick; return "Vita Controller"; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *joystick) { return (SDL_JoystickID)(uintptr_t)joystick; }
int SDL_JoystickNumAxes(SDL_Joystick *joystick) { (void)joystick; return 4; }
int SDL_JoystickNumBalls(SDL_Joystick *joystick) { (void)joystick; return 0; }
int SDL_JoystickNumHats(SDL_Joystick *joystick) { (void)joystick; return 0; }
int SDL_JoystickNumButtons(SDL_Joystick *joystick) { (void)joystick; return 16; }

void SDL_JoystickUpdate(void) {}
int SDL_JoystickGetButton(SDL_Joystick *joystick, int button) {
    (void)joystick; (void)button;
    /* Handled via event system */
    return 0;
}

Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis) {
    (void)joystick; (void)axis;
    return 0;
}

Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat) {
    (void)joystick; (void)hat;
    return SDL_HAT_CENTERED;
}

int SDL_JoystickGetBall(SDL_Joystick *joystick, int ball, int *dx, int *dy) {
    (void)joystick; (void)ball; (void)dx; (void)dy;
    return -1;
}

void SDL_JoystickClose(SDL_Joystick *joystick) {
    int idx = ((uintptr_t)joystick) - 1;
    if (idx >= 0 && idx < SDL_JOYSTICK_MAX_DEVICES) {
        sdl_state.joystick_attached[idx] = SDL_FALSE;
        sdl_state.joystick_count--;
    }
}

int SDL_JoystickEventState(int state) {
    (void)state;
    return SDL_ENABLE;
}

/* GameController */

const char *SDL_GameControllerNameForIndex(int joystick_index) {
    (void)joystick_index;
    return "PS Vita Controller";
}

SDL_bool SDL_IsGameController(int joystick_index) {
    return (joystick_index == 0) ? SDL_TRUE : SDL_FALSE;
}

SDL_GameController *SDL_GameControllerOpen(int joystick_index) {
    return (SDL_GameController*)SDL_JoystickOpen(joystick_index);
}

const char *SDL_GameControllerName(SDL_GameController *gc) {
    return "PS Vita Controller";
}

SDL_bool SDL_GameControllerGetAttached(SDL_GameController *gc) {
    return SDL_JoystickGetAttached((SDL_Joystick*)gc);
}

SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gc) {
    return (SDL_Joystick*)gc;
}

int SDL_GameControllerEventState(int state) {
    (void)state;
    return SDL_ENABLE;
}

void SDL_GameControllerUpdate(void) {}

Sint16 SDL_GameControllerGetAxis(SDL_GameController *gc, SDL_GameControllerAxis axis) {
    (void)gc; (void)axis;
    return 0;
}

Uint8 SDL_GameControllerGetButton(SDL_GameController *gc, SDL_GameControllerButton button) {
    (void)gc; (void)button;
    return 0;
}

void SDL_GameControllerClose(SDL_GameController *gc) {
    SDL_JoystickClose((SDL_Joystick*)gc);
}

int SDL_GameControllerAddMapping(const char *mappingString) { (void)mappingString; return 0; }
int SDL_GameControllerNumMappings(void) { return 0; }
char *SDL_GameControllerMapping(SDL_GameController *gc) { (void)gc; return NULL; }
char *SDL_GameControllerMappingForDeviceIndex(int idx) { (void)idx; return NULL; }
const char *SDL_GameControllerGetStringForAxis(SDL_GameControllerAxis axis) { (void)axis; return ""; }
const char *SDL_GameControllerGetStringForButton(SDL_GameControllerButton btn) { (void)btn; return ""; }

/* ======================================================
 * Event System
 * ====================================================== */

static SDL_Event event_queue[64];
static int event_head = 0;
static int event_tail = 0;

static void push_event(const SDL_Event *ev) {
    int next = (event_tail + 1) % 64;
    if (next != event_head) {
        event_queue[event_tail] = *ev;
        event_tail = next;
    }
}

enum {
    VITA_ANALOG_LOW = 64,
    VITA_ANALOG_HIGH = 192
};

static int vita_binding_active(const SceCtrlData *pad,
                               unsigned int vita_mask,
                               SDL_Scancode scancode) {
    int active = (pad->buttons & vita_mask) != 0;

    /* Treat the left stick as a second directional source.  Combining it
     * here with the D-pad prevents one source from releasing an arrow while
     * the other source is still held. */
    switch (scancode) {
    case SDL_SCANCODE_LEFT:  active |= pad->lx < VITA_ANALOG_LOW; break;
    case SDL_SCANCODE_RIGHT: active |= pad->lx > VITA_ANALOG_HIGH; break;
    case SDL_SCANCODE_UP:    active |= pad->ly < VITA_ANALOG_LOW; break;
    case SDL_SCANCODE_DOWN:  active |= pad->ly > VITA_ANALOG_HIGH; break;
    default: break;
    }

    return active;
}

void SDL_PumpEvents(void) {
    /* Poll Vita controller and generate events */
    if (!(sdl_state.init_flags & SDL_INIT_EVENTS)) {
        sceCtrlPeekBufferPositive(0, &sdl_state.pad, 1);
        return;
    }
    
    SceCtrlData new_pad;
    memset(&new_pad, 0, sizeof(new_pad));
    sceCtrlPeekBufferPositive(0, &new_pad, 1);
    
    /* Detect button changes and generate keyboard events */
    static SceCtrlData old_pad;
    
    /* Mapping: Vita buttons → SDL keyboard scancodes */
    static const struct {
        unsigned int vita_mask;
        SDL_Scancode scancode;
    } button_map[] = {
        /* RGSS A/B/C/X/Y/Z/L/R, with Square also exposing Shift so the
         * standard RPG Maker sprint binding works on every engine version. */
        { SCE_CTRL_CROSS,     SDL_SCANCODE_RETURN }, /* C / confirm */
        { SCE_CTRL_CIRCLE,    SDL_SCANCODE_ESCAPE }, /* B / cancel */
        { SCE_CTRL_SQUARE,    SDL_SCANCODE_LSHIFT }, /* A / Shift / sprint */
        { SCE_CTRL_TRIANGLE,  SDL_SCANCODE_A },      /* X */
        { SCE_CTRL_UP,        SDL_SCANCODE_UP },
        { SCE_CTRL_DOWN,      SDL_SCANCODE_DOWN },
        { SCE_CTRL_LEFT,      SDL_SCANCODE_LEFT },
        { SCE_CTRL_RIGHT,     SDL_SCANCODE_RIGHT },
        { SCE_CTRL_START,     SDL_SCANCODE_S },      /* Y */
        { SCE_CTRL_SELECT,    SDL_SCANCODE_D },      /* Z */
        { SCE_CTRL_LTRIGGER,  SDL_SCANCODE_Q },      /* L */
        { SCE_CTRL_RTRIGGER,  SDL_SCANCODE_W },      /* R */
        { 0, 0 }
    };
    
    for (int i = 0; button_map[i].vita_mask; i++) {
        unsigned int mask = button_map[i].vita_mask;
        SDL_Scancode scan = button_map[i].scancode;
        
        int was_pressed = vita_binding_active(&old_pad, mask, scan);
        int is_pressed = vita_binding_active(&new_pad, mask, scan);
        
        if (is_pressed && !was_pressed) {
            /* Key down */
            SDL_Event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = SDL_KEYDOWN;
            ev.key.state = SDL_PRESSED;
            ev.key.keysym.scancode = scan;
            ev.key.keysym.sym = SDL_GetKeyFromScancode(scan);
            push_event(&ev);
            sdl_state.keyboard_state[scan] = 1;
        } else if (!is_pressed && was_pressed) {
            /* Key up */
            SDL_Event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = SDL_KEYUP;
            ev.key.state = SDL_RELEASED;
            ev.key.keysym.scancode = scan;
            ev.key.keysym.sym = SDL_GetKeyFromScancode(scan);
            push_event(&ev);
            sdl_state.keyboard_state[scan] = 0;
        }
    }
    
    old_pad = new_pad;
}

int SDL_PollEvent(SDL_Event *event) {
    SDL_PumpEvents();
    
    if (event_head == event_tail) {
        return 0; /* No events */
    }
    
    if (event) {
        *event = event_queue[event_head];
    }
    event_head = (event_head + 1) % 64;
    return 1;
}

int SDL_WaitEvent(SDL_Event *event) {
    while (!SDL_PollEvent(event)) {
        sceKernelDelayThread(10000); /* 10ms */
    }
    return 1;
}

int SDL_WaitEventTimeout(SDL_Event *event, int timeout) {
    Uint32 start = SDL_GetTicks();
    while (!SDL_PollEvent(event)) {
        if (timeout >= 0 && (SDL_GetTicks() - start) >= (Uint32)timeout) {
            return 0;
        }
        sceKernelDelayThread(10000);
    }
    return 1;
}

int SDL_PushEvent(SDL_Event *event) {
    if (event) push_event(event);
    return 1;
}

int SDL_PeepEvents(SDL_Event *events, int numevents, int action, Uint32 minType, Uint32 maxType) {
    (void)events; (void)numevents; (void)action; (void)minType; (void)maxType;
    return 0;
}

SDL_bool SDL_HasEvent(Uint32 type) {
    (void)type;
    return SDL_FALSE;
}

SDL_bool SDL_HasEvents(Uint32 minType, Uint32 maxType) {
    (void)minType; (void)maxType;
    return SDL_FALSE;
}

void SDL_FlushEvent(Uint32 type) { (void)type; }
void SDL_FlushEvents(Uint32 minType, Uint32 maxType) { (void)minType; (void)maxType; }

int SDL_EventState(Uint32 type, int state) {
    (void)type; (void)state;
    return SDL_ENABLE;
}

Uint32 SDL_RegisterEvents(int numevents) {
    (void)numevents;
    return SDL_USEREVENT;
}

void SDL_SetEventFilter(SDL_EventFilter *filter, void *userdata) {
    (void)filter; (void)userdata;
}

SDL_bool SDL_GetEventFilter(SDL_EventFilter **filter, void **userdata) {
    if (filter) *filter = NULL;
    if (userdata) *userdata = NULL;
    return SDL_FALSE;
}

/* ======================================================
 * Audio (Stub — basic SceAudio output)
 * ====================================================== */

int SDL_GetNumAudioDrivers(void) { return 1; }
const char *SDL_GetAudioDriver(int index) { (void)index; return "vita"; }
int SDL_AudioInit(const char *driver_name) { (void)driver_name; return 0; }
void SDL_AudioQuit(void) {}
const char *SDL_GetCurrentAudioDriver(void) { return "vita"; }

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
    (void)desired; (void)obtained;
    return -1; /* Use SDL_OpenAudioDevice instead */
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture, 
    const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes) {
    (void)device; (void)allowed_changes;

    if (iscapture)
        return 0;
    
    if (!sdl_state.audio_initialized) {
        int channels = desired && desired->channels == 1 ? 1 : 2;
        int mode = channels == 2 ? SCE_AUDIO_OUT_MODE_STEREO
                                 : SCE_AUDIO_OUT_MODE_MONO;
        sdl_state.audio_samples = 512;
        sdl_state.audio_channels = channels;
        /* MAIN audibly distorts even a sine wave on real Vita hardware.
         * The BGM port is clean for the same PCM format and block sizes. */
        sdl_state.audio_port = sceAudioOutOpenPort(
            SCE_AUDIO_OUT_PORT_TYPE_BGM, sdl_state.audio_samples, 48000, mode);
        if (sdl_state.audio_port < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to open audio port: %d", sdl_state.audio_port);
            return 0;
        }
        sdl_state.audio_initialized = 1;
        SDL_Log("Audio initialized (port: %d)", sdl_state.audio_port);
    }

    if (obtained) {
        memset(obtained, 0, sizeof(*obtained));
        obtained->freq = 48000;
        obtained->format = AUDIO_S16SYS;
        obtained->channels = (Uint8)sdl_state.audio_channels;
        obtained->samples = (Uint16)sdl_state.audio_samples;
        obtained->size = (Uint32)(sdl_state.audio_samples *
                                  sdl_state.audio_channels * sizeof(short));
    }
    
    return 1; /* Non-zero device ID */
}

void SDL_PauseAudio(int pause_on) {
    sdl_state.audio_paused = pause_on;
}

void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) {
    (void)dev;
    sdl_state.audio_paused = pause_on;
}

void SDL_CloseAudio(void) {
    if (sdl_state.audio_port >= 0) {
        sceAudioOutReleasePort(sdl_state.audio_port);
        sdl_state.audio_port = -1;
    }
    sdl_state.audio_initialized = 0;
    sdl_state.audio_channels = 0;
    sdl_state.audio_samples = 0;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {
    (void)dev;
    SDL_CloseAudio();
}

int SDL_GetAudioStatus(void) {
    return sdl_state.audio_paused ? SDL_AUDIO_PAUSED : SDL_AUDIO_PLAYING;
}

int SDL_GetAudioDeviceStatus(SDL_AudioDeviceID dev) {
    (void)dev;
    return SDL_GetAudioStatus();
}

int SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, Uint32 len) {
    (void)dev;
    if (sdl_state.audio_paused || sdl_state.audio_port < 0) return 0;

    const Uint8 *cursor = (const Uint8 *)data;
    Uint32 block_bytes = (Uint32)(sdl_state.audio_samples *
                                  sdl_state.audio_channels * sizeof(short));
    short block[512 * 2];
    while (len > 0) {
        Uint32 take = len < block_bytes ? len : block_bytes;
        memset(block, 0, block_bytes);
        memcpy(block, cursor, take);
        int rc = sceAudioOutOutput(sdl_state.audio_port, block);
        if (rc < 0)
            return SDL_SetError("sceAudioOutOutput failed: %d", rc);
        cursor += take;
        len -= take;
    }
    return 0;
}

Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev) {
    (void)dev;
    return 0;
}

void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev) {
    (void)dev;
}

void SDL_LockAudio(void) {}
void SDL_LockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void SDL_UnlockAudio(void) {}
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }

void SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len, int volume) {
    for (Uint32 i = 0; i < len; i++) {
        dst[i] = (Uint8)(((int)dst[i] + ((int)src[i] - 128) * volume / SDL_MIX_MAXVOLUME + 128));
    }
}

void SDL_MixAudioFormat(Uint8 *dst, const Uint8 *src, Uint16 format, Uint32 len, int volume) {
    (void)format;
    SDL_MixAudio(dst, src, len, volume);
}

/* ======================================================
 * Threading (pthreads-based)
 * ====================================================== */

#include <pthread.h>

struct SDL_Thread {
    pthread_t thread;
    SDL_threadID id;
    char name[32];
    SDL_ThreadFunction func;
    void *data;
    int retval;
};

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data) {
    return SDL_CreateThreadWithStackSize(fn, name, 0, data);
}

static void *thread_wrapper(void *arg) {
    SDL_Thread *t = (SDL_Thread*)arg;
    t->retval = t->func(t->data);
    return NULL;
}

SDL_Thread *SDL_CreateThreadWithStackSize(SDL_ThreadFunction fn, const char *name, 
    const size_t stacksize, void *data) {
    SDL_Thread *t = calloc(1, sizeof(SDL_Thread));
    if (!t) return NULL;
    
    t->func = fn;
    t->data = data;
    if (name) strncpy(t->name, name, sizeof(t->name) - 1);
    
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stacksize > 0) {
        pthread_attr_setstacksize(&attr, stacksize);
    }
    
    if (pthread_create(&t->thread, &attr, thread_wrapper, t) != 0) {
        free(t);
        return NULL;
    }
    
    t->id = (SDL_threadID)t->thread;
    pthread_attr_destroy(&attr);
    return t;
}

const char *SDL_GetThreadName(SDL_Thread *thread) {
    return thread ? thread->name : "main";
}

SDL_threadID SDL_GetThreadID(SDL_Thread *thread) {
    return thread ? thread->id : (SDL_threadID)pthread_self();
}

SDL_threadID SDL_ThreadID(void) {
    return (SDL_threadID)pthread_self();
}

int SDL_SetThreadPriority(SDL_ThreadPriority priority) {
    (void)priority;
    return 0;
}

int SDL_WaitThread(SDL_Thread *thread, int *status) {
    if (!thread) return -1;
    pthread_join(thread->thread, NULL);
    if (status) *status = thread->retval;
    int ret = thread->retval;
    free(thread);
    return ret;
}

void SDL_DetachThread(SDL_Thread *thread) {
    if (thread) {
        pthread_detach(thread->thread);
        free(thread);
    }
}

/* ======================================================
 * Mutex / Semaphore / Condition Variables
 * ====================================================== */

struct SDL_mutex {
    pthread_mutex_t mutex;
};

SDL_mutex *SDL_CreateMutex(void) {
    SDL_mutex *m = calloc(1, sizeof(SDL_mutex));
    if (!m) return NULL;

    /* SDL mutexes are recursive.  The default POSIX mutex is not, and
     * mkxp-z legitimately nests its graphics lock while disposal signals
     * connected resources.  Keep the Vita replacement compatible with SDL2
     * instead of deadlocking the RGSS thread on the second acquisition. */
    pthread_mutexattr_t attr;
    int rc = pthread_mutexattr_init(&attr);
    if (rc == 0)
        rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (rc == 0)
        rc = pthread_mutex_init(&m->mutex, &attr);
    if (pthread_mutexattr_destroy(&attr) != 0 && rc == 0)
        rc = -1;
    if (rc != 0 || !m->mutex) {
        SDL_SetError("pthread_mutex_init failed (%d)", rc);
        free(m);
        return NULL;
    }
    return m;
}

void SDL_DestroyMutex(SDL_mutex *mutex) {
    if (mutex) {
        if (mutex->mutex)
            pthread_mutex_destroy(&mutex->mutex);
        free(mutex);
    }
}

int SDL_LockMutex(SDL_mutex *mutex) {
    return (mutex && mutex->mutex) ? pthread_mutex_lock(&mutex->mutex) : -1;
}

int SDL_TryLockMutex(SDL_mutex *mutex) {
    return (mutex && mutex->mutex) ? pthread_mutex_trylock(&mutex->mutex) : -1;
}

int SDL_UnlockMutex(SDL_mutex *mutex) {
    return (mutex && mutex->mutex) ? pthread_mutex_unlock(&mutex->mutex) : -1;
}

struct SDL_semaphore {
    Uint32 count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

SDL_sem *SDL_CreateSemaphore(Uint32 initial_value) {
    SDL_sem *sem = calloc(1, sizeof(SDL_sem));
    if (!sem) return NULL;
    sem->count = initial_value;
    int mutex_rc = pthread_mutex_init(&sem->mutex, NULL);
    if (mutex_rc != 0 || !sem->mutex) {
        SDL_SetError("pthread_mutex_init failed (%d)", mutex_rc);
        free(sem);
        return NULL;
    }
    int cond_rc = pthread_cond_init(&sem->cond, NULL);
    if (cond_rc != 0 || !sem->cond) {
        SDL_SetError("pthread_cond_init failed (%d)", cond_rc);
        pthread_mutex_destroy(&sem->mutex);
        free(sem);
        return NULL;
    }
    return sem;
}

void SDL_DestroySemaphore(SDL_sem *sem) {
    if (sem) {
        if (sem->mutex)
            pthread_mutex_destroy(&sem->mutex);
        if (sem->cond)
            pthread_cond_destroy(&sem->cond);
        free(sem);
    }
}

int SDL_SemWait(SDL_sem *sem) {
    if (!sem || !sem->mutex || !sem->cond) return -1;
    pthread_mutex_lock(&sem->mutex);
    while (sem->count == 0) {
        pthread_cond_wait(&sem->cond, &sem->mutex);
    }
    sem->count--;
    pthread_mutex_unlock(&sem->mutex);
    return 0;
}

int SDL_SemTryWait(SDL_sem *sem) {
    if (!sem || !sem->mutex || !sem->cond) return -1;
    pthread_mutex_lock(&sem->mutex);
    if (sem->count == 0) {
        pthread_mutex_unlock(&sem->mutex);
        return -1;
    }
    sem->count--;
    pthread_mutex_unlock(&sem->mutex);
    return 0;
}

int SDL_SemWaitTimeout(SDL_sem *sem, Uint32 ms) {
    /* Simple implementation */
    Uint32 start = SDL_GetTicks();
    while (SDL_GetTicks() - start < ms) {
        if (SDL_SemTryWait(sem) == 0) return 0;
        sceKernelDelayThread(1000);
    }
    return -1;
}

int SDL_SemPost(SDL_sem *sem) {
    if (!sem || !sem->mutex || !sem->cond) return -1;
    pthread_mutex_lock(&sem->mutex);
    sem->count++;
    pthread_cond_signal(&sem->cond);
    pthread_mutex_unlock(&sem->mutex);
    return 0;
}

Uint32 SDL_SemValue(SDL_sem *sem) {
    return sem->count;
}

struct SDL_cond {
    pthread_cond_t cond;
};

SDL_cond *SDL_CreateCond(void) {
    SDL_cond *c = calloc(1, sizeof(SDL_cond));
    if (!c) return NULL;
    int rc = pthread_cond_init(&c->cond, NULL);
    if (rc != 0 || !c->cond) {
        SDL_SetError("pthread_cond_init failed (%d)", rc);
        free(c);
        return NULL;
    }
    return c;
}

void SDL_DestroyCond(SDL_cond *cond) {
    if (cond) {
        if (cond->cond)
            pthread_cond_destroy(&cond->cond);
        free(cond);
    }
}

int SDL_CondSignal(SDL_cond *cond) {
    return (cond && cond->cond) ? pthread_cond_signal(&cond->cond) : -1;
}

int SDL_CondBroadcast(SDL_cond *cond) {
    return (cond && cond->cond) ? pthread_cond_broadcast(&cond->cond) : -1;
}

int SDL_CondWait(SDL_cond *cond, SDL_mutex *mutex) {
    return (cond && cond->cond && mutex && mutex->mutex) ?
           pthread_cond_wait(&cond->cond, &mutex->mutex) : -1;
}

int SDL_CondWaitTimeout(SDL_cond *cond, SDL_mutex *mutex, Uint32 ms) {
    if (!cond || !cond->cond || !mutex || !mutex->mutex) return -1;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    return pthread_cond_timedwait(&cond->cond, &mutex->mutex, &ts);
}

/* ======================================================
 * RWops
 * ====================================================== */

static Sint64 rw_size(SDL_RWops *ctx) {
    long current;
    long end;
    if (!ctx || !ctx->hidden.stdio.fp)
        return -1;
    current = ftell(ctx->hidden.stdio.fp);
    if (current < 0 || fseek(ctx->hidden.stdio.fp, 0, SEEK_END) != 0)
        return -1;
    end = ftell(ctx->hidden.stdio.fp);
    fseek(ctx->hidden.stdio.fp, current, SEEK_SET);
    return end >= 0 ? (Sint64)end : -1;
}
static Sint64 rw_seek(SDL_RWops *ctx, Sint64 offset, int whence) {
    if (!ctx || !ctx->hidden.stdio.fp) return -1;
    if (fseek(ctx->hidden.stdio.fp, (long)offset, whence) != 0) return -1;
    return (Sint64)ftell(ctx->hidden.stdio.fp);
}
static size_t rw_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum) {
    if (!ctx || !ctx->hidden.stdio.fp) return 0;
    return fread(ptr, size, maxnum, ctx->hidden.stdio.fp);
}
static size_t rw_write(SDL_RWops *ctx, const void *ptr, size_t size, size_t num) {
    if (!ctx || !ctx->hidden.stdio.fp) return 0;
    return fwrite(ptr, size, num, ctx->hidden.stdio.fp);
}
static int rw_close(SDL_RWops *ctx) {
    if (!ctx) return -1;
    if (ctx->hidden.stdio.fp && ctx->hidden.stdio.autoclose) {
        fclose(ctx->hidden.stdio.fp);
    }
    free(ctx);
    return 0;
}

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode) {
    FILE *fp = fopen(file, mode);
    if (!fp) return NULL;
    
    SDL_RWops *ops = SDL_AllocRW();
    if (!ops) { fclose(fp); return NULL; }
    
    ops->hidden.stdio.fp = fp;
    ops->hidden.stdio.autoclose = 1;
    ops->size = rw_size;
    ops->seek = rw_seek;
    ops->read = rw_read;
    ops->write = rw_write;
    ops->close = rw_close;
    return ops;
}

SDL_RWops *SDL_RWFromFP(FILE *fp, SDL_bool autoclose) {
    SDL_RWops *ops = SDL_AllocRW();
    if (!ops) return NULL;
    ops->hidden.stdio.fp = fp;
    ops->hidden.stdio.autoclose = autoclose;
    ops->size = rw_size;
    ops->seek = rw_seek;
    ops->read = rw_read;
    ops->write = rw_write;
    ops->close = rw_close;
    return ops;
}

typedef struct VitaRWMemory {
    const unsigned char *data;
    size_t size;
    size_t position;
    int writable;
} VitaRWMemory;

static VitaRWMemory *rw_memory(SDL_RWops *ctx) {
    return ctx ? (VitaRWMemory *)ctx->hidden.unknown.data1 : NULL;
}

static Sint64 rw_mem_size(SDL_RWops *ctx) {
    VitaRWMemory *mem = rw_memory(ctx);
    return mem ? (Sint64)mem->size : -1;
}

static Sint64 rw_mem_seek(SDL_RWops *ctx, Sint64 offset, int whence) {
    VitaRWMemory *mem = rw_memory(ctx);
    Sint64 position;
    if (!mem) return -1;
    if (whence == RW_SEEK_SET) position = offset;
    else if (whence == RW_SEEK_CUR) position = (Sint64)mem->position + offset;
    else if (whence == RW_SEEK_END) position = (Sint64)mem->size + offset;
    else return -1;
    if (position < 0 || position > (Sint64)mem->size) return -1;
    mem->position = (size_t)position;
    return position;
}

static size_t rw_mem_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum) {
    VitaRWMemory *mem = rw_memory(ctx);
    size_t available;
    size_t requested;
    if (!mem || !ptr || size == 0 || maxnum == 0) return 0;
    available = mem->size - mem->position;
    requested = size * maxnum;
    if (requested > available) requested = available - (available % size);
    memcpy(ptr, mem->data + mem->position, requested);
    mem->position += requested;
    return requested / size;
}

static size_t rw_mem_write(SDL_RWops *ctx, const void *ptr, size_t size, size_t num) {
    VitaRWMemory *mem = rw_memory(ctx);
    size_t available;
    size_t requested;
    if (!mem || !mem->writable || !ptr || size == 0 || num == 0) return 0;
    available = mem->size - mem->position;
    requested = size * num;
    if (requested > available) requested = available - (available % size);
    memcpy((unsigned char *)mem->data + mem->position, ptr, requested);
    mem->position += requested;
    return requested / size;
}

static int rw_mem_close(SDL_RWops *ctx) {
    if (!ctx) return -1;
    free(ctx->hidden.unknown.data1);
    free(ctx);
    return 0;
}

static SDL_RWops *rw_from_memory(const void *mem, int size, int writable) {
    SDL_RWops *ops;
    VitaRWMemory *state;
    if (!mem || size < 0) return NULL;
    ops = SDL_AllocRW();
    state = (VitaRWMemory *)calloc(1, sizeof(*state));
    if (!ops || !state) {
        free(ops);
        free(state);
        return NULL;
    }
    state->data = (const unsigned char *)mem;
    state->size = (size_t)size;
    state->writable = writable;
    ops->hidden.unknown.data1 = state;
    ops->size = rw_mem_size;
    ops->seek = rw_mem_seek;
    ops->read = rw_mem_read;
    ops->write = rw_mem_write;
    ops->close = rw_mem_close;
    return ops;
}

SDL_RWops *SDL_RWFromMem(void *mem, int size) {
    return rw_from_memory(mem, size, 1);
}

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size) {
    return rw_from_memory(mem, size, 0);
}

SDL_RWops *SDL_AllocRW(void) {
    SDL_RWops *ops = calloc(1, sizeof(SDL_RWops));
    if (ops) {
        ops->type = 0xDEADBEEF;
        ops->size = rw_size;
        ops->seek = rw_seek;
        ops->read = rw_read;
        ops->write = rw_write;
        ops->close = rw_close;
    }
    return ops;
}

void SDL_FreeRW(SDL_RWops *area) {
    free(area);
}

Sint64 SDL_RWsize(SDL_RWops *context) {
    return context && context->size ? context->size(context) : -1;
}

Sint64 SDL_RWseek(SDL_RWops *context, Sint64 offset, int whence) {
    return context && context->seek ? context->seek(context, offset, whence) : -1;
}

size_t SDL_RWread(SDL_RWops *context, void *ptr, size_t size, size_t maxnum) {
    return context && context->read ? context->read(context, ptr, size, maxnum) : 0;
}

size_t SDL_RWwrite(SDL_RWops *context, const void *ptr, size_t size, size_t num) {
    return context && context->write ? context->write(context, ptr, size, num) : 0;
}

Sint64 SDL_RWtell(SDL_RWops *context) {
    return SDL_RWseek(context, 0, RW_SEEK_CUR);
}

int SDL_RWclose(SDL_RWops *context) {
    return context && context->close ? context->close(context) : -1;
}

Uint16 SDL_ReadLE16(SDL_RWops *src) {
    Uint16 val;
    SDL_RWread(src, &val, sizeof(val), 1);
    return SDL_SwapLE16(val);
}

Uint32 SDL_ReadLE32(SDL_RWops *src) {
    Uint32 val;
    SDL_RWread(src, &val, sizeof(val), 1);
    return SDL_SwapLE32(val);
}

Uint64 SDL_ReadLE64(SDL_RWops *src) {
    Uint64 val;
    SDL_RWread(src, &val, sizeof(val), 1);
    return SDL_SwapLE64(val);
}

Sint16 SDL_ReadBE16(SDL_RWops *src) {
    Uint16 val;
    SDL_RWread(src, &val, sizeof(val), 1);
    return (Sint16)SDL_SwapBE16(val);
}

Sint32 SDL_ReadBE32(SDL_RWops *src) {
    Uint32 val;
    SDL_RWread(src, &val, sizeof(val), 1);
    return (Sint32)SDL_SwapBE32(val);
}

/* ======================================================
 * Rect functions
 * ====================================================== */

SDL_bool SDL_HasIntersection(const SDL_Rect *A, const SDL_Rect *B) {
    SDL_Rect dummy;
    return SDL_IntersectRect(A, B, &dummy);
}

SDL_bool SDL_IntersectRect(const SDL_Rect *A, const SDL_Rect *B, SDL_Rect *result) {
    if (!A || !B || !result) return SDL_FALSE;
    int x1 = (A->x > B->x) ? A->x : B->x;
    int y1 = (A->y > B->y) ? A->y : B->y;
    int x2 = ((A->x + A->w) < (B->x + B->w)) ? (A->x + A->w) : (B->x + B->w);
    int y2 = ((A->y + A->h) < (B->y + B->h)) ? (A->y + A->h) : (B->y + B->h);
    if (x1 >= x2 || y1 >= y2) {
        result->x = result->y = result->w = result->h = 0;
        return SDL_FALSE;
    }
    result->x = x1; result->y = y1;
    result->w = x2 - x1; result->h = y2 - y1;
    return SDL_TRUE;
}

void SDL_UnionRect(const SDL_Rect *A, const SDL_Rect *B, SDL_Rect *result) {
    if (!A || !B || !result) return;
    int x1 = (A->x < B->x) ? A->x : B->x;
    int y1 = (A->y < B->y) ? A->y : B->y;
    int x2 = ((A->x + A->w) > (B->x + B->w)) ? (A->x + A->w) : (B->x + B->w);
    int y2 = ((A->y + A->h) > (B->y + B->h)) ? (A->y + A->h) : (B->y + B->h);
    result->x = x1; result->y = y1;
    result->w = x2 - x1; result->h = y2 - y1;
}

SDL_bool SDL_EnclosePoints(const SDL_Point *points, int count, const SDL_Rect *clip, SDL_Rect *result) {
    if (!points || count < 1) return SDL_FALSE;
    int x1 = points[0].x, y1 = points[0].y;
    int x2 = x1, y2 = y1;
    for (int i = 1; i < count; i++) {
        if (points[i].x < x1) x1 = points[i].x;
        if (points[i].y < y1) y1 = points[i].y;
        if (points[i].x > x2) x2 = points[i].x;
        if (points[i].y > y2) y2 = points[i].y;
    }
    if (result) { result->x = x1; result->y = y1; result->w = x2 - x1; result->h = y2 - y1; }
    return SDL_TRUE;
}

SDL_bool SDL_RectEmpty(const SDL_Rect *r) {
    return (!r || r->w <= 0 || r->h <= 0) ? SDL_TRUE : SDL_FALSE;
}

SDL_bool SDL_RectEquals(const SDL_Rect *a, const SDL_Rect *b) {
    return (a && b && a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h) ? SDL_TRUE : SDL_FALSE;
}

/* ======================================================
 * Assert
 * ====================================================== */

SDL_AssertState SDL_ReportAssertion(SDL_AssertData *data, const char *func, const char *file, int line) {
    fprintf(stderr, "ASSERT: %s (%s:%d) - %s\n", data->condition, file, line, func);
    return SDL_ASSERTION_ABORT;
}
