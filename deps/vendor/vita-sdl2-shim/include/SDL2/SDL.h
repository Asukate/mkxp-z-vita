/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
/**
 * SDL2 Vita Shim — Main Include
 * Implements a subset of SDL2 API for PS Vita homebrew
 */
#ifndef SDL_H
#define SDL_H

#include "SDL_stdinc.h"
#include "SDL_error.h"
#include "SDL_log.h"
#include "SDL_platform.h"
#include "SDL_version.h"
#include "SDL_hints.h"
#include "SDL_timer.h"
#include "SDL_rect.h"
#include "SDL_video.h"
#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_scancode.h"
#include "SDL_keycode.h"
#include "SDL_joystick.h"
#include "SDL_gamecontroller.h"
#include "SDL_audio.h"
#include "SDL_thread.h"
#include "SDL_mutex.h"
#include "SDL_rwops.h"
#include "SDL_endian.h"
#include "SDL_cpuinfo.h"
#include "SDL_assert.h"
#include "SDL_touch.h"
#include "SDL_surface.h"
#include "SDL_messagebox.h"
#include "SDL_loadso.h"
#include "SDL_image.h"
#include "SDL_clipboard.h"

/* Init flags */
#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_JOYSTICK       0x00000200u
#define SDL_INIT_HAPTIC         0x00001000u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_EVENTS         0x00004000u
#define SDL_INIT_SENSOR         0x00008000u
#define SDL_INIT_EVERYTHING ( \
    SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO | \
    SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | \
    SDL_INIT_HAPTIC \
)

/* Quit */
#define SDL_QUIT  0x100
#define SDL_GL_RED_SIZE               0
#define SDL_GL_GREEN_SIZE             1
#define SDL_GL_BLUE_SIZE              2
#define SDL_GL_ALPHA_SIZE             3
#define SDL_GL_BUFFER_SIZE            4
#define SDL_GL_DOUBLEBUFFER           5
#define SDL_GL_DEPTH_SIZE             6
#define SDL_GL_STENCIL_SIZE           7
#define SDL_GL_ACCUM_RED_SIZE         8
#define SDL_GL_ACCUM_GREEN_SIZE       9
#define SDL_GL_ACCUM_BLUE_SIZE       10
#define SDL_GL_ACCUM_ALPHA_SIZE      11
#define SDL_GL_STEREO                12
#define SDL_GL_MULTISAMPLEBUFFERS    13
#define SDL_GL_MULTISAMPLESAMPLES    14
#define SDL_GL_ACCELERATED_VISUAL    15
#define SDL_GL_RETAINED_BACKING      16
#define SDL_GL_CONTEXT_MAJOR_VERSION 17
#define SDL_GL_CONTEXT_MINOR_VERSION 18
#define SDL_GL_CONTEXT_EGL           19
#define SDL_GL_CONTEXT_FLAGS         20
#define SDL_GL_CONTEXT_PROFILE_MASK  21
#define SDL_GL_SHARE_WITH_CURRENT_CONTEXT 22
#define SDL_GL_FRAMEBUFFER_SRGB_CAPABLE 23
#define SDL_GL_CONTEXT_RELEASE_BEHAVIOR 24
#define SDL_GL_CONTEXT_RESET_NOTIFICATION 25
#define SDL_GL_CONTEXT_NO_ERROR      26
#define SDL_GL_FLOATBUFFERS          27

#define SDL_GL_CONTEXT_PROFILE_CORE          0x0001
#define SDL_GL_CONTEXT_PROFILE_COMPATIBILITY 0x0002
#define SDL_GL_CONTEXT_PROFILE_ES            0x0004

#define SDL_GL_CONTEXT_DEBUG_FLAG             0x0001
#define SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG 0x0002
#define SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG     0x0004
#define SDL_GL_CONTEXT_RESET_ISOLATION_FLAG   0x0008

/* Audio format */
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16SYS AUDIO_S16LSB

#define SDL_TEXTUREACCESS_STATIC   0
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET   2

#define SDL_TEXTUREDIRECTION_NONE  0
#define SDL_TEXTUREDIRECTION_HORIZONTAL 1
#define SDL_TEXTUREDIRECTION_VERTICAL 2

/* Common pixel formats */
#define SDL_PIXELTYPE_UNKNOWN 0
#define SDL_PIXELTYPE_INDEX1 1
#define SDL_PIXELTYPE_INDEX4 2
#define SDL_PIXELTYPE_INDEX8 3
#define SDL_PIXELTYPE_PACKED8 4
#define SDL_PIXELTYPE_PACKED16 5
#define SDL_PIXELTYPE_PACKED32 6
#define SDL_PIXELTYPE_ARRAYU8 7
#define SDL_PIXELTYPE_ARRAYU16 8
#define SDL_PIXELTYPE_ARRAYU32 9
#define SDL_PIXELTYPE_ARRAYF16 10
#define SDL_PIXELTYPE_ARRAYF32 11

#define SDL_PIXELLE_NONE 0
#define SDL_PIXELLE_332 1
#define SDL_PIXELLE_4444 2
#define SDL_PIXELLE_1555 3
#define SDL_PIXELLE_5551 4
#define SDL_PIXELLE_565 5
#define SDL_PIXELLE_8888 6
#define SDL_PIXELLE_RGB888 7
#define SDL_PIXELLE_BGR888 8
#define SDL_PIXELLE_ARGB8888 9
#define SDL_PIXELLE_RGBA8888 10
#define SDL_PIXELLE_ABGR8888 11
#define SDL_PIXELLE_BGRA8888 12
#define SDL_PIXELLE_ARGB2101010 13

/* Pixel format constants */
#include "SDL_pixels.h"

/* Blend mode */
#include "SDL_blendmode.h"

/* Functions */
#ifdef __cplusplus
extern "C" {
#endif

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
void SDL_Quit(void);
void SDL_QuitSubSystem(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);

/* Platform */
const char *SDL_GetPlatform(void);

/* CPU info */
int SDL_GetCPUCount(void);
int SDL_GetCPUCacheLineSize(void);

/* Endian */
Uint16 SDL_Swap16(Uint16 x);
Uint32 SDL_Swap32(Uint32 x);
Uint64 SDL_Swap64(Uint64 x);

/* RWops */
struct SDL_RWops;
typedef struct SDL_RWops SDL_RWops;
SDL_RWops *SDL_AllocRW(void);
void SDL_FreeRW(SDL_RWops *area);

#ifdef __cplusplus
}
#endif

#endif /* SDL_H */
