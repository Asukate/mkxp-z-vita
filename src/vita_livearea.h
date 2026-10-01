#ifndef MKXPZ_VITA_LIVEAREA_H
#define MKXPZ_VITA_LIVEAREA_H

#ifdef __vita__
/*
 * Inspect the Vita LiveArea boot event.  Returns true when the user selected
 * the configuration liveitem instead of the normal Start gate.
 *
 * When true is returned, the function has attempted to hand off execution to
 * app0:/configurator.bin and the caller should stop normal mkxp-z startup.
 */
bool vitaHandleLiveAreaLaunch();
#endif

#endif
