/* Derived from SDL 2.30.0 (https://github.com/libsdl-org/SDL): trimmed
 * and adapted for the Vita shim; Doxygen markup removed, enums named.
 * Original: Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>,
 * zlib license -- see vendor/vita-sdl2/LICENSE-SDL.txt. */
#ifndef SDL_AUDIO_H
#define SDL_AUDIO_H

#include "SDL_stdinc.h"
#include "SDL_rwops.h"
#include "SDL_mutex.h"

#define AUDIO_U8        0x0008
#define AUDIO_S8        0x8008
#define AUDIO_U16LSB    0x0010
#define AUDIO_S16LSB    0x8010
#define AUDIO_U16MSB    0x1010
#define AUDIO_S16MSB    0x9010
#define AUDIO_U16       AUDIO_U16LSB
#define AUDIO_S16       AUDIO_S16LSB
#define AUDIO_S32LSB    0x8020
#define AUDIO_S32MSB    0x9020
#define AUDIO_F32LSB    0x8120
#define AUDIO_F32MSB    0x9120
#define AUDIO_S16SYS    AUDIO_S16LSB
#define AUDIO_F32SYS    AUDIO_F32LSB

typedef struct {
    int freq;
    int format;
    int channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    void (*callback)(void *userdata, Uint8 *stream, int len);
    void *userdata;
} SDL_AudioSpec;

typedef Uint32 SDL_AudioDeviceID;

#define SDL_AUDIO_STOPPED 0
#define SDL_AUDIO_PLAYING 1
#define SDL_AUDIO_PAUSED  2

#define SDL_MIX_MAXVOLUME 128

#ifdef __cplusplus
extern "C" {
#endif

int SDL_GetNumAudioDrivers(void);
const char *SDL_GetAudioDriver(int index);
int SDL_AudioInit(const char *driver_name);
void SDL_AudioQuit(void);
const char *SDL_GetCurrentAudioDriver(void);
int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes);
void SDL_PauseAudio(int pause_on);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void SDL_CloseAudio(void);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
int SDL_GetAudioStatus(void);
int SDL_GetAudioDeviceStatus(SDL_AudioDeviceID dev);
int SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, Uint32 len);
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev);
void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev);
void SDL_LockAudio(void);
void SDL_LockAudioDevice(SDL_AudioDeviceID dev);
void SDL_UnlockAudio(void);
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev);
void SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len, int volume);
void SDL_MixAudioFormat(Uint8 *dst, const Uint8 *src, Uint16 format, Uint32 len, int volume);

#ifdef __cplusplus
}
#endif

#endif /* SDL_AUDIO_H */
