/* Minimal always-on Vita startup telemetry.
 *
 * Unlike the verbose diagnostic logger, this is safe for release builds: it
 * writes only a handful of flushed phase markers and lets the RGSS title probe
 * report the exact elapsed wall time from process entry.
 */
#ifndef MKXPZ_VITA_STARTUP_TIMER_H
#define MKXPZ_VITA_STARTUP_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void vitaStartupTimerInit(void);
void vitaStartupTimerMark(const char *phase);
uint64_t vitaStartupTimerElapsedUs(void);
void vitaStartupTimerShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
