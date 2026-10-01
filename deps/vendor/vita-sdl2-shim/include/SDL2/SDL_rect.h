/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_RECT_H
#define SDL_RECT_H

#include "SDL_stdinc.h"

typedef struct SDL_Rect {
    int x, y;
    int w, h;
} SDL_Rect;

typedef struct SDL_Point {
    int x, y;
} SDL_Point;

typedef struct SDL_FPoint {
    float x, y;
} SDL_FPoint;

typedef struct SDL_FRect {
    float x, y;
    float w, h;
} SDL_FRect;

#define SDL_HAS_RECT 1

#ifdef __cplusplus
extern "C" {
#endif

SDL_bool SDL_HasIntersection(const SDL_Rect *A, const SDL_Rect *B);
SDL_bool SDL_IntersectRect(const SDL_Rect *A, const SDL_Rect *B, SDL_Rect *result);
void SDL_UnionRect(const SDL_Rect *A, const SDL_Rect *B, SDL_Rect *result);
SDL_bool SDL_EnclosePoints(const SDL_Point *points, int count, const SDL_Rect *clip, SDL_Rect *result);
SDL_bool SDL_RectEmpty(const SDL_Rect *r);
SDL_bool SDL_RectEquals(const SDL_Rect *a, const SDL_Rect *b);
SDL_bool SDL_PointInRect(const SDL_Point *p, const SDL_Rect *r);

#ifdef __cplusplus
}
#endif

#endif /* SDL_RECT_H */
