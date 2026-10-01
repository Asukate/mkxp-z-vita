/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_VIDEO_H
#define SDL_VIDEO_H

#include "SDL_stdinc.h"
#include "SDL_rect.h"

typedef void *SDL_GLContext;
typedef struct SDL_Window SDL_Window;

#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000
#define SDL_WINDOWPOS_CENTERED 0x2FFF0000
#define SDL_WINDOW_INPUT_FOCUS 0x00800000u
#define SDL_WINDOW_MOUSE_FOCUS 0x00400000u
#define SDL_WINDOW_FULLSCREEN 0x00000001u
#define SDL_WINDOW_OPENGL 0x00000002u
#define SDL_WINDOW_SHOWN 0x00000004u
#define SDL_WINDOW_HIDDEN 0x00000008u
#define SDL_WINDOW_BORDERLESS 0x00000010u
#define SDL_WINDOW_RESIZABLE 0x00000020u
#define SDL_WINDOW_MINIMIZED 0x00000040u
#define SDL_WINDOW_MAXIMIZED 0x00000080u
#define SDL_WINDOW_INPUT_GRABBED 0x00000100u
#define SDL_WINDOW_ALLOW_HIGHDPI 0x00002000u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000)

typedef enum {
    SDL_GL_RED_SIZE,
    SDL_GL_GREEN_SIZE,
    SDL_GL_BLUE_SIZE,
    SDL_GL_ALPHA_SIZE,
    SDL_GL_BUFFER_SIZE,
    SDL_GL_DOUBLEBUFFER,
    SDL_GL_DEPTH_SIZE,
    SDL_GL_STENCIL_SIZE,
    SDL_GL_ACCUM_RED_SIZE,
    SDL_GL_ACCUM_GREEN_SIZE,
    SDL_GL_ACCUM_BLUE_SIZE,
    SDL_GL_ACCUM_ALPHA_SIZE,
    SDL_GL_STEREO,
    SDL_GL_MULTISAMPLEBUFFERS,
    SDL_GL_MULTISAMPLESAMPLES,
    SDL_GL_ACCELERATED_VISUAL,
    SDL_GL_RETAINED_BACKING,
    SDL_GL_CONTEXT_MAJOR_VERSION,
    SDL_GL_CONTEXT_MINOR_VERSION,
    SDL_GL_CONTEXT_EGL,
    SDL_GL_CONTEXT_FLAGS,
    SDL_GL_CONTEXT_PROFILE_MASK,
    SDL_GL_SHARE_WITH_CURRENT_CONTEXT,
    SDL_GL_FRAMEBUFFER_SRGB_CAPABLE,
    SDL_GL_CONTEXT_RELEASE_BEHAVIOR,
    SDL_GL_CONTEXT_RESET_NOTIFICATION,
    SDL_GL_CONTEXT_NO_ERROR,
    SDL_GL_FLOATBUFFERS
} SDL_GLattr;

/* GL attributes — values defined in SDL.h */
#define SDL_GL_CONTEXT_PROFILE_CORE          0x0001
#define SDL_GL_CONTEXT_PROFILE_COMPATIBILITY 0x0002
#define SDL_GL_CONTEXT_PROFILE_ES            0x0004

#ifdef __cplusplus
extern "C" {
#endif

int SDL_GetNumVideoDisplays(void);
const char *SDL_GetDisplayName(int displayIndex);
int SDL_GetDisplayBounds(int displayIndex, SDL_Rect *rect);
int SDL_GetDisplayDPI(int displayIndex, float *ddpi, float *hdpi, float *vdpi);

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *window);
void SDL_SetWindowTitle(SDL_Window *window, const char *title);
const char *SDL_GetWindowTitle(SDL_Window *window);
void SDL_SetWindowSize(SDL_Window *window, int w, int h);
void SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
Uint32 SDL_GetWindowFlags(SDL_Window *window);
void SDL_ShowWindow(SDL_Window *window);
void SDL_HideWindow(SDL_Window *window);
void SDL_RaiseWindow(SDL_Window *window);
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags);

int SDL_GL_LoadLibrary(const char *path);
void *SDL_GL_GetProcAddress(const char *proc);
int SDL_GL_SetAttribute(int attr, int value);
int SDL_GL_GetAttribute(int attr, int *value);
SDL_GLContext SDL_GL_CreateContext(SDL_Window *window);
void SDL_GL_DeleteContext(SDL_GLContext context);
int SDL_GL_MakeCurrent(SDL_Window *window, SDL_GLContext context);
SDL_Window *SDL_GL_GetCurrentWindow(void);
SDL_GLContext SDL_GL_GetCurrentContext(void);
int SDL_GL_SetSwapInterval(int interval);
int SDL_GL_GetSwapInterval(void);
void SDL_GL_SwapWindow(SDL_Window *window);
Uint32 SDL_GetWindowID(SDL_Window *window);
typedef struct SDL_DisplayMode SDL_DisplayMode;
int SDL_GetDisplayMode(int displayIndex, int modeIndex, SDL_DisplayMode *mode);
typedef struct SDL_Surface SDL_Surface;
void SDL_SetWindowIcon(SDL_Window *window, SDL_Surface *icon);
int SDL_GetCurrentDisplayMode(int displayIndex, SDL_DisplayMode *mode);
int SDL_GetWindowDisplayMode(SDL_Window *window, SDL_DisplayMode *mode);

/* Display mode */
typedef struct SDL_DisplayMode {
    Uint32 format;
    int w;
    int h;
    int refresh_rate;
    void *driverdata;
} SDL_DisplayMode;

SDL_Surface *SDL_GetWindowSurface(SDL_Window *window);
int SDL_UpdateWindowSurface(SDL_Window *window);
void SDL_SetWindowPosition(SDL_Window *window, int x, int y);
int SDL_GetWindowDisplayIndex(SDL_Window *window);
int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode);
void SDL_GL_GetDrawableSize(SDL_Window *window, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif /* SDL_VIDEO_H */
