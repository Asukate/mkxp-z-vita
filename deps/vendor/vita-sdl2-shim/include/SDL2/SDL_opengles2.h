/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_OPENGLEG2_H_
#define SDL_OPENGLEG2_H_
/* GLES2 for Vita — wraps vitaGL headers */
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* Don't define APIENTRYP here — gl-fun.h maps it to GL_APIENTRYP */
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef GL_APIENTRY
#define GL_APIENTRY
#endif
#ifndef GL_APIENTRYP
#define GL_APIENTRYP *
#endif
#ifndef GL_APICALL
#define GL_APICALL
#endif

/* GL constant missing from Vita's GLES2 headers */
#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B30
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#endif
