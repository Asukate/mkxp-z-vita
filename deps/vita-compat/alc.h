#ifndef ALC_H
#define ALC_H
#include "al.h"
/* ALC types — must be defined before function declarations */
#ifndef ALCboolean
#define ALCboolean int
#endif
#ifndef ALCchar
#define ALCchar char
#endif
#ifndef ALCvoid
#define ALCvoid void
#endif
#ifndef ALCint
#define ALCint int
#endif
#ifndef ALCuint
#define ALCuint unsigned int
#endif
#ifndef ALCenum
#define ALCenum int
#endif
#ifndef ALCsizei
#define ALCsizei int
#endif

typedef struct ALCdevice ALCdevice;
typedef struct ALCcontext ALCcontext;
#define ALC_DEFAULT_DEVICE_SPECIFIER 0x1004
#define ALC_DEVICE_SPECIFIER 0x1005
#define ALC_EXTENSIONS 0x1006
#define ALC_FREQUENCY 0x1007
#define ALC_NO_ERROR 0
#ifdef __cplusplus
extern "C" {
#endif
ALCdevice *alcOpenDevice(const char *devicename);
ALCdevice *alcCaptureOpenDevice(const char *devicename, ALCuint frequency, ALCenum format, ALCsizei buffersize);
ALCboolean alcCaptureCloseDevice(ALCdevice *device);
ALCboolean alcCloseDevice(ALCdevice *device);
ALCcontext *alcCreateContext(ALCdevice *device, const ALCint *attrlist);
ALCboolean alcMakeContextCurrent(ALCcontext *context);
void alcProcessContext(ALCcontext *context);
void alcSuspendContext(ALCcontext *context);
void alcDestroyContext(ALCcontext *context);
ALCcontext *alcGetCurrentContext(void);
ALCdevice *alcGetContextsDevice(ALCcontext *context);
ALCboolean alcIsExtensionPresent(ALCdevice *device, const ALCchar *extname);
void alcGetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values);
const ALCchar *alcGetString(ALCdevice *device, ALCenum param);
ALCenum alcGetError(ALCdevice *device);
void alcCaptureStart(ALCdevice *device);
void alcCaptureStop(ALCdevice *device);
void alcCaptureSamples(ALCdevice *device, ALCvoid *buffer, ALCsizei samples);
void *alcGetProcAddress(ALCdevice *device, const ALCchar *funcname);
#ifdef __cplusplus
}
#endif
#endif
