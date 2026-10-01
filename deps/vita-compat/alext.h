#ifndef ALEXT_H_
#define ALEXT_H_
/* Minimal alext.h stub for Vita cross-compile */
#include <al.h>
#include <alc.h>

#ifndef APIENTRY
#define APIENTRY
#endif

#ifndef ALC_APIENTRY
#define ALC_APIENTRY
#endif

#ifndef ALC_CALL
#define ALC_CALL
#endif

#ifndef ALCboolean
#define ALCboolean int
#endif

#ifndef ALCchar
#define ALCchar char
#endif

#ifndef ALCvoid
#define ALCvoid void
#endif

/* Extension tokens - define the ones mkxp-z might reference */
#ifndef ALC_ENUMERATE_ALL_EXT
#define ALC_ENUMERATE_ALL_EXT 1
#define ALC_DEFAULT_ALL_DEVICES_SPECIFIER 0x1012
#define ALC_ALL_DEVICES_SPECIFIER 0x1013
#endif

/* alcGetProcAddress - needed by mkxp-z for extension loading */
#ifndef ALC_NO_AUDIO
void *alcGetProcAddress(ALCdevice *device, const ALCchar *funcname);
#endif

#endif
