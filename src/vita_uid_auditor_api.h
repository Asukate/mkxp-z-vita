#ifndef MKXPZ_VITA_UID_AUDITOR_API_H
#define MKXPZ_VITA_UID_AUDITOR_API_H

#if defined(__vita__) && defined(MKXPZ_VITA_DIAGNOSTICS)
#include "../tools/diagnostics/vita_uid_auditor/vita_uid_auditor.h"
#else
static inline void ba_script_begin(void) {}
static inline void ba_snapshot(const char *) {}
static inline void ba_dump_final(const char *) {}
#endif

#endif
