/*
 * Vita-only startup and crash diagnostics.
 *
 * The diagnostic build writes a short, flushed milestone log to ux0: and
 * supports a simple SELECT/file trigger for a system psp2dump.  The
 * non-diagnostic build keeps these calls as no-ops so the instrumentation does
 * not become part of the normal runtime contract accidentally.
 */

#ifndef MKXPZ_VITA_DIAGNOSTIC_H
#define MKXPZ_VITA_DIAGNOSTIC_H

#include <stdbool.h>

#ifdef MKXPZ_VITA_DIAGNOSTICS

void vitaDiagInit(const char *argv0);
void vitaDiagShutdown();
bool vitaDiagTextProfileEnabled();
bool vitaDiagAtlasTraceEnabled();
#ifdef __cplusplus
extern "C" {
#endif
void vitaDiagLog(const char *tag, const char *format, ...);
void vitaDiagLogThread(const char *phase);
void vitaDiagLogMemory(const char *phase);
void vitaDiagLogVglMemory(const char *phase);
#ifdef __cplusplus
}
#endif
void vitaDiagLogVglPool(const char *phase);
void vitaDiagPollTrigger();
void vitaDiagControllerButton(int button, bool pressed);
void vitaDiagS42IArmTailTrace();
void vitaDiagTextureStorage(unsigned int id, int width, int height);
void vitaDiagTextureDelete(unsigned int id);
unsigned long long vitaDiagTextureUploadBytes();
void vitaDiagGpuSnapshot(const char *phase);
/* Ruby VM/GVL held, before acquiring the graphics lock or native pointers. */
void vitaGpuPressureSafePoint(const char *phase);

#else

static inline void vitaDiagInit(const char *) {}
static inline void vitaDiagShutdown() {}
static inline bool vitaDiagTextProfileEnabled() { return false; }
static inline bool vitaDiagAtlasTraceEnabled() { return false; }
/* Do not evaluate diagnostic arguments in normal builds. */
#define vitaDiagLog(...) ((void)0)
static inline void vitaDiagLogThread(const char *) {}
static inline void vitaDiagLogMemory(const char *) {}
static inline void vitaDiagLogVglMemory(const char *) {}
static inline void vitaDiagLogVglPool(const char *) {}
static inline void vitaDiagPollTrigger() {}
static inline void vitaDiagControllerButton(int, bool) {}
static inline void vitaDiagS42IArmTailTrace() {}
static inline void vitaDiagTextureStorage(unsigned int, int, int) {}
static inline void vitaDiagTextureDelete(unsigned int) {}
static inline unsigned long long vitaDiagTextureUploadBytes() { return 0; }
static inline void vitaDiagGpuSnapshot(const char *) {}
static inline void vitaGpuPressureSafePoint(const char *) {}

#endif

#endif
