/* HardRPG - minimal local SDL_sound API declarations
 * Copyright (C) 2026 Asukate
 *
 * This file is part of HardRPG, free software under the GNU
 * General Public License v3. See LICENSES/GPL-3.0.txt at the repository root. */
#ifndef SDL_SOUND_H_
#define SDL_SOUND_H_
#include "SDL_stdinc.h"
#include "SDL_rwops.h"
#include "SDL_surface.h"
#ifdef __cplusplus
extern "C" {
#endif

/* SDL_sound stub types */
typedef struct Sound_AudioInfo {
    Uint16 format;
    Uint8 channels;
    Uint32 rate;
} Sound_AudioInfo;

typedef struct Sound_Sample {
    void *opaque;
    Uint8 *buffer;
    Uint32 buffer_size;
    int rate;
    Uint8 format;
    Uint8 channels;
    Uint32 flags;
    Sound_AudioInfo actual;
} Sound_Sample;

#define SOUND_SAMPLEFLAG_NONE 0
#define SOUND_SAMPLEFLAG_CANSEEK 1
#define SOUND_SAMPLEFLAG_EOF 2
#define SOUND_SAMPLEFLAG_ERROR 4
#define SOUND_SAMPLEFLAG_EAGAIN 8

Uint16 Sound_GetNativeAudioFormat(void);
Sound_Sample *Sound_NewSample(SDL_RWops *rwops, const char *ext, const Sound_AudioInfo *desired, Uint32 bufferSize);
void Sound_FreeSample(Sound_Sample *sample);
Uint32 Sound_Decode(Sound_Sample *sample);
Uint32 Sound_DecodeAll(Sound_Sample *sample);
void Sound_Rewind(Sound_Sample *sample);
void Sound_SetVolume(Sound_Sample *sample, int volume);
int Sound_Seek(Sound_Sample *sample, Uint32 ms);

/* Init/quit/stubs */
int Sound_Init(void);
void Sound_Quit(void);
const char *Sound_GetError(void);

#ifdef __cplusplus
}
#endif
#endif
