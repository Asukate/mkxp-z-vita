#ifndef SDL_mouse_h_
#define SDL_mouse_h_
#include "SDL_stdinc.h"
#include "SDL_video.h"
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_X1 6
#define SDL_BUTTON_X2 7
#define SDL_BUTTON(X) (1 << ((X)-1))
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { Uint32 buttons; int x, y; } SDL_Cursor;
SDL_Cursor *SDL_CreateCursor(const Uint8 *data, const Uint8 *mask, int w, int h, int hot_x, int hot_y);
void SDL_FreeCursor(SDL_Cursor *cursor);
void SDL_SetCursor(SDL_Cursor *cursor);
SDL_Cursor *SDL_GetCursor(void);
int SDL_ShowCursor(int toggle);
void SDL_WarpMouseInWindow(SDL_Window *window, int x, int y);
int SDL_GetMouseState(int *x, int *y);
int SDL_GetRelativeMouseState(int *x, int *y);
void SDL_SetRelativeMouseMode(SDL_bool enabled);
#ifdef __cplusplus
}
#endif
#endif
