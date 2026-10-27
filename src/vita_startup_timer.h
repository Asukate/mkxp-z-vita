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
/* Keep this run's marks as startup-timing.prev.log: the next process
 * (a restart into the launcher or a game) starts the log again. */
void vitaStartupTimerKeep(void);
/* Launcher bench builds only: the marks so far, in process time. */
int vitaStartupTimerBenchMarks(const char **phases, uint64_t *us, int max);

#ifdef __cplusplus
}
#endif

#endif
