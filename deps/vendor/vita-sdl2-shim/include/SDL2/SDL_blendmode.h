/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_blendmode_h_
#define SDL_blendmode_h_
typedef enum {
    SDL_BLENDMODE_NONE = 0x00000000,
    SDL_BLENDMODE_BLEND = 0x00000001,
    SDL_BLENDMODE_ADD = 0x00000002,
    SDL_BLENDMODE_MOD = 0x00000004,
    SDL_BLENDMODE_MUL = 0x00000008,
    SDL_BLENDMODE_INVALID = 0x7FFFFFFF
} SDL_BlendMode;

/* TTF style flags — defined here for convenience (mkxp-z uses them) */
#define TTF_STYLE_NORMAL       0x00
#define TTF_STYLE_BOLD         0x01
#define TTF_STYLE_ITALIC       0x02
#define TTF_STYLE_UNDERLINE    0x04
#define TTF_STYLE_STRIKETHROUGH 0x08
#endif
