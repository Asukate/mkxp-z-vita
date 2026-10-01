#ifndef HARDRPG_VITA_UID_AUDITOR_H
#define HARDRPG_VITA_UID_AUDITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum BaUidFamily {
    BA_UID_THREAD = 1,
    BA_UID_SEMA = 2,
    BA_UID_MUTEX = 3,
    BA_UID_LWMUTEX = 4,
    BA_UID_EVENTFLAG = 5,
    BA_UID_MSGPIPE = 6,
    BA_UID_MEMBLOCK = 7,
    BA_UID_FAMILY_COUNT = 8
};

void ba_script_begin(void);
void ba_snapshot(const char *milestone);
void ba_dump_final(const char *reason);
int32_t ba_live_total_get(void);
int32_t ba_script_live_get(void);
uint32_t ba_unknown_delete_get(void);

#ifdef __cplusplus
}
#endif

#endif
