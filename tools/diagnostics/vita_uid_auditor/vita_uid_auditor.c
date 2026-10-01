/* S42-BA: fixed-storage Vita kernel UID caller attribution. */
#include "vita_uid_auditor.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef BA_HOST_TEST
typedef int32_t SceUID;
typedef uint32_t SceUInt;
typedef uint32_t SceSize;
typedef int SceKernelMemBlockType;
typedef int (*SceKernelThreadEntry)(SceSize, void *);
typedef struct { uint64_t data[4]; } SceKernelLwMutexWork;
typedef struct { uint32_t size; } SceKernelLwMutexOptParam;
typedef struct { uint32_t size; } SceKernelSemaOptParam;
typedef struct { uint32_t size; } SceKernelMutexOptParam;
typedef struct { uint32_t size; } SceKernelThreadOptParam;
typedef struct { uint32_t size; } SceKernelEventFlagOptParam;
typedef struct { uint32_t size; } SceKernelAllocMemBlockOpt;
extern SceUID sceKernelGetThreadId(void);
extern int sceClibPrintf(const char *format, ...);
#else
#include <errno.h>
#include <pthread.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <semaphore.h>
#endif

extern void vitaDiagLog(const char *tag, const char *format, ...);

#define BA_CALLER_CAP 512u
#define BA_LIVE_CAP 4096u
#define BA_EVENT_CAP 256u
#define BA_TOP_CAP 64u
#define BA_UID_RL_OVERFLOW ((int32_t)0x8002450d)

struct ba_caller_stat {
    uintptr_t caller;
    uintptr_t first_uid, last_uid;
    uint32_t create_attempts, create_successes, create_failures;
    uint32_t delete_attempts, delete_successes, delete_failures;
    uint32_t matched_deletes, unknown_deletes, duplicate_deletes, duplicate_creates;
    uint32_t script_create_successes, script_delete_successes;
    uint32_t script_create_failures, script_delete_failures;
    uint32_t script_matched_deletes;
    uint32_t first_create_seq, last_create_seq;
    uint32_t first_create_tid, last_create_tid;
    int32_t current, highwater, script_current, script_highwater;
    uint8_t family, used;
};

struct ba_live_rec {
    uintptr_t identity, creator;
    uint32_t create_seq, create_tid;
    uint8_t family, state; /* 0 empty, 1 live, 2 tombstone */
};

struct ba_event {
    uintptr_t identity, caller;
    uint32_t seq, tid;
    int32_t result, live_total, script_live, caller_current;
    uint8_t family, operation;
};

static struct ba_caller_stat ba_callers[BA_CALLER_CAP];
static struct ba_live_rec ba_live[BA_LIVE_CAP];
static struct ba_event ba_events[BA_EVENT_CAP];
static struct ba_caller_stat ba_dump_callers[BA_CALLER_CAP];
static struct ba_event ba_dump_events[BA_EVENT_CAP];
static int32_t ba_dump_families[BA_UID_FAMILY_COUNT];
static uint32_t ba_dump_family_creates[BA_UID_FAMILY_COUNT];
static uint32_t ba_dump_family_deletes[BA_UID_FAMILY_COUNT];
static uint32_t ba_dump_family_failures[BA_UID_FAMILY_COUNT];
static volatile int ba_state_lock, ba_dump_lock, ba_logging;
static uint32_t ba_seq, ba_event_count, ba_unknown_deletes, ba_duplicate_deletes;
static uint32_t ba_duplicate_creates, ba_table_overflow;
static uint32_t ba_threshold_mask;
static int32_t ba_live_total, ba_script_live;
static int32_t ba_live_family[BA_UID_FAMILY_COUNT];
static uint32_t ba_family_creates[BA_UID_FAMILY_COUNT];
static uint32_t ba_family_deletes[BA_UID_FAMILY_COUNT];
static uint32_t ba_family_failures[BA_UID_FAMILY_COUNT];
static uint32_t ba_sem_init_calls, ba_sem_init_failures;
static uint32_t ba_sem_destroy_calls, ba_sem_destroy_failures;
static uint32_t ba_mutex_init_calls, ba_mutex_init_failures;
static uint32_t ba_mutex_destroy_calls, ba_mutex_destroy_failures;
static uint32_t ba_cond_init_calls, ba_cond_init_failures;
static uint32_t ba_cond_destroy_calls, ba_cond_destroy_failures;
static int ba_script_active, ba_overflow_reported;
static const int32_t ba_thresholds[] = {100, 200, 400, 800, 1200, 1600, 1900};

static void ba_lock(volatile int *lock) { while (__sync_lock_test_and_set(lock, 1)) {} }
static void ba_unlock(volatile int *lock) { __sync_lock_release(lock); }
static uint32_t ba_thread_id(void) { return (uint32_t)sceKernelGetThreadId(); }

static const char *ba_family_name(uint8_t family)
{
    switch (family) {
    case BA_UID_THREAD: return "thread";
    case BA_UID_SEMA: return "sema";
    case BA_UID_MUTEX: return "mutex";
    case BA_UID_LWMUTEX: return "lwmutex";
    case BA_UID_EVENTFLAG: return "eventflag";
    case BA_UID_MSGPIPE: return "msgpipe";
    case BA_UID_MEMBLOCK: return "memblock";
    default: return "unknown";
    }
}

static uint32_t ba_hash(uint8_t family, uintptr_t value, uint32_t cap)
{
    uint32_t v = (uint32_t)value;
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15;
    v *= 0x846ca68bu; v ^= v >> 16;
    return (v ^ ((uint32_t)family * 0x9e3779b9u)) & (cap - 1u);
}

static struct ba_caller_stat *ba_get_caller(uint8_t family, uintptr_t caller)
{
    uint32_t start = ba_hash(family, caller, BA_CALLER_CAP), i;
    for (i = 0; i < BA_CALLER_CAP; ++i) {
        struct ba_caller_stat *s = &ba_callers[(start + i) & (BA_CALLER_CAP - 1u)];
        if (!s->used) {
            memset(s, 0, sizeof(*s));
            s->used = 1; s->family = family; s->caller = caller;
            return s;
        }
        if (s->family == family && s->caller == caller) return s;
    }
    ++ba_table_overflow;
    return NULL;
}

static struct ba_live_rec *ba_find_live(uint8_t family, uintptr_t identity)
{
    uint32_t start = ba_hash(family, identity, BA_LIVE_CAP), i;
    for (i = 0; i < BA_LIVE_CAP; ++i) {
        struct ba_live_rec *r = &ba_live[(start + i) & (BA_LIVE_CAP - 1u)];
        if (r->state == 0) return NULL;
        if (r->state == 1 && r->family == family && r->identity == identity) return r;
    }
    return NULL;
}

static int ba_was_deleted(uint8_t family, uintptr_t identity)
{
    uint32_t start = ba_hash(family, identity, BA_LIVE_CAP), i;
    for (i = 0; i < BA_LIVE_CAP; ++i) {
        struct ba_live_rec *r = &ba_live[(start + i) & (BA_LIVE_CAP - 1u)];
        if (r->state == 0) return 0;
        if (r->state == 2 && r->family == family && r->identity == identity) return 1;
    }
    return 0;
}

static int ba_insert_live(uint8_t family, uintptr_t identity, uintptr_t creator,
                          uint32_t seq, uint32_t tid)
{
    uint32_t start = ba_hash(family, identity, BA_LIVE_CAP), i;
    struct ba_live_rec *tombstone = NULL;
    for (i = 0; i < BA_LIVE_CAP; ++i) {
        struct ba_live_rec *r = &ba_live[(start + i) & (BA_LIVE_CAP - 1u)];
        if (r->state == 1 && r->family == family && r->identity == identity) return 0;
        if (r->state == 2 && !tombstone) tombstone = r;
        if (r->state == 0) {
            if (tombstone) r = tombstone;
            r->identity = identity; r->creator = creator;
            r->create_seq = seq; r->create_tid = tid;
            r->family = family; r->state = 1;
            return 1;
        }
    }
    if (tombstone) {
        tombstone->identity = identity; tombstone->creator = creator;
        tombstone->create_seq = seq; tombstone->create_tid = tid;
        tombstone->family = family; tombstone->state = 1;
        return 1;
    }
    ++ba_table_overflow;
    return 0;
}

static void ba_event_add(uint8_t family, char operation, uintptr_t identity,
                         uintptr_t caller, int32_t result, uint32_t seq,
                         uint32_t tid, int32_t caller_current)
{
    struct ba_event *e = &ba_events[(seq - 1u) & (BA_EVENT_CAP - 1u)];
    e->identity = identity; e->caller = caller; e->seq = seq; e->tid = tid;
    e->result = result; e->live_total = ba_live_total; e->script_live = ba_script_live;
    e->caller_current = caller_current;
    e->family = family; e->operation = (uint8_t)operation;
    if (ba_event_count < BA_EVENT_CAP) ++ba_event_count;
}

static int ba_crossed_threshold_locked(void)
{
    uint32_t i;
    if (!ba_script_active) return -1;
    for (i = 0; i < sizeof(ba_thresholds) / sizeof(ba_thresholds[0]); ++i) {
        uint32_t bit = 1u << i;
        if (!(ba_threshold_mask & bit) && ba_live_total >= ba_thresholds[i]) {
            ba_threshold_mask |= bit;
            return (int)i;
        }
    }
    return -1;
}

static int ba_record_create(uint8_t family, uintptr_t identity,
                            uintptr_t caller, int32_t result)
{
    struct ba_caller_stat *s;
    uint32_t seq, tid = ba_thread_id();
    int threshold = -1;
    ba_lock(&ba_state_lock);
    seq = ++ba_seq; s = ba_get_caller(family, caller);
    if (s) {
        ++s->create_attempts;
    }
    if (result >= 0) {
        int inserted = ba_insert_live(family, identity, caller, seq, tid);
        if (s) {
            ++s->create_successes;
            s->last_create_seq = seq; s->last_create_tid = tid;
            s->last_uid = identity;
            if (!s->first_create_seq) {
                s->first_create_seq = seq; s->first_create_tid = tid;
                s->first_uid = identity;
            }
            if (ba_script_active) ++s->script_create_successes;
            if (inserted) {
                if (++s->current > s->highwater) s->highwater = s->current;
                if (ba_script_active && ++s->script_current > s->script_highwater)
                    s->script_highwater = s->script_current;
            }
            else {
                ++s->duplicate_creates;
            }
        }
        if (!inserted) ++ba_duplicate_creates;
        if (inserted) {
            ++ba_live_total;
            if (family < BA_UID_FAMILY_COUNT) {
                ++ba_live_family[family]; ++ba_family_creates[family];
            }
            if (ba_script_active) ++ba_script_live;
        }
        ba_event_add(family, 'C', identity, caller, result, seq, tid,
                     s ? s->current : -1);
        threshold = ba_crossed_threshold_locked();
    } else {
        if (s) {
            ++s->create_failures;
            if (ba_script_active) ++s->script_create_failures;
        }
        if (family < BA_UID_FAMILY_COUNT) ++ba_family_failures[family];
        ba_event_add(family, 'F', identity, caller, result, seq, tid,
                     s ? s->current : -1);
    }
    ba_unlock(&ba_state_lock);
    return threshold;
}

static void ba_record_delete(uint8_t family, uintptr_t identity,
                             uintptr_t caller, int32_t result)
{
    struct ba_caller_stat *ds, *cs;
    struct ba_live_rec *live;
    uint32_t seq, tid = ba_thread_id();
    ba_lock(&ba_state_lock);
    seq = ++ba_seq; ds = ba_get_caller(family, caller);
    if (ds) {
        ++ds->delete_attempts;
    }
    if (result < 0) {
        if (ds) {
            ++ds->delete_failures;
            if (ba_script_active) ++ds->script_delete_failures;
        }
        if (family < BA_UID_FAMILY_COUNT) ++ba_family_failures[family];
        ba_event_add(family, 'F', identity, caller, result, seq, tid,
                     ds ? ds->current : -1);
        ba_unlock(&ba_state_lock); return;
    }
    if (ds) {
        ++ds->delete_successes;
        if (ba_script_active) ++ds->script_delete_successes;
    }
    live = ba_find_live(family, identity);
    if (!live) {
        int duplicate = ba_was_deleted(family, identity);
        if (duplicate) {
            ++ba_duplicate_deletes;
            if (ds) ++ds->duplicate_deletes;
        } else {
            ++ba_unknown_deletes;
            if (ds) ++ds->unknown_deletes;
        }
        ba_event_add(family, duplicate ? 'X' : 'U', identity, caller, result, seq, tid,
                     ds ? ds->current : -1);
        ba_unlock(&ba_state_lock); return;
    }
    cs = ba_get_caller(family, live->creator); live->state = 2;
    if (cs) {
        ++cs->matched_deletes;
        if (ba_script_active) ++cs->script_matched_deletes;
        if (cs->current > 0) --cs->current;
        if (ba_script_active && cs->script_current > 0) --cs->script_current;
    }
    if (ba_live_total > 0) --ba_live_total;
    if (family < BA_UID_FAMILY_COUNT) {
        ++ba_family_deletes[family];
        if (ba_live_family[family] > 0) --ba_live_family[family];
    }
    if (ba_script_active && ba_script_live > 0) --ba_script_live;
    ba_event_add(family, 'D', identity, caller, result, seq, tid,
                 cs ? cs->current : -1);
    ba_unlock(&ba_state_lock);
}

static int ba_stat_better(const struct ba_caller_stat *a, const struct ba_caller_stat *b)
{
    if (a->script_current != b->script_current) return a->script_current > b->script_current;
    if (a->current != b->current) return a->current > b->current;
    if (a->create_successes != b->create_successes) return a->create_successes > b->create_successes;
    return a->caller < b->caller;
}

static void ba_dump(const char *reason, int include_ring)
{
    uint16_t top[BA_TOP_CAP];
    uint32_t caller_count = 0, top_count = 0, i, event_count, event_start;
    int32_t live_total, script_live;
    uint32_t unknown, duplicate_delete, duplicate_create, table_overflow, seq;
    if (__sync_lock_test_and_set(&ba_dump_lock, 1)) return;
    __atomic_store_n(&ba_logging, 1, __ATOMIC_RELEASE);
    ba_lock(&ba_state_lock);
    memcpy(ba_dump_callers, ba_callers, sizeof(ba_callers));
    memcpy(ba_dump_events, ba_events, sizeof(ba_events));
    memcpy(ba_dump_families, ba_live_family, sizeof(ba_live_family));
    memcpy(ba_dump_family_creates, ba_family_creates, sizeof(ba_family_creates));
    memcpy(ba_dump_family_deletes, ba_family_deletes, sizeof(ba_family_deletes));
    memcpy(ba_dump_family_failures, ba_family_failures, sizeof(ba_family_failures));
    event_count = ba_event_count; seq = ba_seq;
    live_total = ba_live_total; script_live = ba_script_live;
    unknown = ba_unknown_deletes; duplicate_delete = ba_duplicate_deletes;
    duplicate_create = ba_duplicate_creates; table_overflow = ba_table_overflow;
    ba_unlock(&ba_state_lock);
    for (i = 0; i < BA_CALLER_CAP; ++i) {
        uint32_t pos;
        if (!ba_dump_callers[i].used) continue;
        ++caller_count;
        for (pos = 0; pos < top_count; ++pos)
            if (ba_stat_better(&ba_dump_callers[i], &ba_dump_callers[top[pos]])) break;
        if (pos < BA_TOP_CAP) {
            uint32_t end = top_count < BA_TOP_CAP ? top_count : BA_TOP_CAP - 1u;
            while (end > pos) { top[end] = top[end - 1u]; --end; }
            top[pos] = (uint16_t)i;
            if (top_count < BA_TOP_CAP) ++top_count;
        }
    }
    vitaDiagLog("BA", "DUMP reason=%s live=%d script_live=%d callers=%u unknown_delete=%u duplicate_delete=%u duplicate_create=%u table_overflow=%u",
                reason ? reason : "-", live_total, script_live, caller_count,
                unknown, duplicate_delete, duplicate_create, table_overflow);
    vitaDiagLog("BA", "PTHREAD sem_init=%u/%u sem_destroy=%u/%u mutex_init=%u/%u mutex_destroy=%u/%u cond_init=%u/%u cond_destroy=%u/%u",
                ba_sem_init_calls, ba_sem_init_failures,
                ba_sem_destroy_calls, ba_sem_destroy_failures,
                ba_mutex_init_calls, ba_mutex_init_failures,
                ba_mutex_destroy_calls, ba_mutex_destroy_failures,
                ba_cond_init_calls, ba_cond_init_failures,
                ba_cond_destroy_calls, ba_cond_destroy_failures);
    if (reason && reason[0] == 'T' && reason[1] == '=')
        vitaDiagLog("BA", "THRESHOLD live=%d script_live=%d marker=%s", live_total, script_live, reason);
    for (i = 1; i < BA_UID_FAMILY_COUNT; ++i)
        vitaDiagLog("BA", "TYPE name=%s live=%d creates=%u deletes=%u failures=%u",
                    ba_family_name((uint8_t)i), ba_dump_families[i],
                    ba_dump_family_creates[i], ba_dump_family_deletes[i],
                    ba_dump_family_failures[i]);
    for (i = 0; i < top_count; ++i) {
        const struct ba_caller_stat *s = &ba_dump_callers[top[i]];
        vitaDiagLog("BA", "CALLER family=%s pc=0x%08x create_attempt=%u create_success=%u create_fail=%u delete_attempt=%u delete_success=%u delete_fail=%u matched_delete=%u current=%d highwater=%d script_create=%u script_delete=%u script_matched_delete=%u script_create_fail=%u script_delete_fail=%u script_live_delta=%d script_highwater=%d unknown_delete=%u duplicate_delete=%u duplicate_create=%u seq=%u/%u tid=0x%08x/0x%08x uid=0x%08x/0x%08x",
                    ba_family_name(s->family), (unsigned)s->caller,
                    s->create_attempts, s->create_successes, s->create_failures,
                    s->delete_attempts, s->delete_successes, s->delete_failures,
                    s->matched_deletes, s->current, s->highwater,
                    s->script_create_successes, s->script_delete_successes,
                    s->script_matched_deletes, s->script_create_failures,
                    s->script_delete_failures, s->script_current,
                    s->script_highwater, s->unknown_deletes,
                    s->duplicate_deletes, s->duplicate_creates,
                    s->first_create_seq, s->last_create_seq,
                    s->first_create_tid, s->last_create_tid,
                    (unsigned)s->first_uid, (unsigned)s->last_uid);
    }
    if (include_ring) {
        event_start = event_count < BA_EVENT_CAP ? 0u : (seq - event_count) & (BA_EVENT_CAP - 1u);
        for (i = 0; i < event_count; ++i) {
            const struct ba_event *e = &ba_dump_events[(event_start + i) & (BA_EVENT_CAP - 1u)];
            vitaDiagLog("BA", "EVENT seq=%u op=%c family=%s id=0x%08x caller=0x%08x tid=0x%08x result=0x%08x live=%d script_live=%d caller_live=%d",
                        e->seq, e->operation, ba_family_name(e->family), (unsigned)e->identity,
                        (unsigned)e->caller, e->tid, (unsigned)e->result,
                        e->live_total, e->script_live, e->caller_current);
        }
    }
    __atomic_store_n(&ba_logging, 0, __ATOMIC_RELEASE);
    ba_unlock(&ba_dump_lock);
}

static void ba_after_create(const char *name, uint8_t family, uintptr_t identity,
                            uintptr_t caller, int32_t result)
{
    int threshold;
    int32_t caller_script_live = 0;
    if (__atomic_load_n(&ba_logging, __ATOMIC_ACQUIRE)) return;
    threshold = ba_record_create(family, identity, caller, result);
    if (result < 0) {
        ba_lock(&ba_state_lock);
        {
            struct ba_caller_stat *s = ba_get_caller(family, caller);
            if (s) caller_script_live = s->script_current;
        }
        ba_unlock(&ba_state_lock);
        sceClibPrintf("[BA] %s FAIL result=0x%08x caller=0x%08x live=%d script_live=%d caller_script_delta=%d\n",
                      name, (unsigned)result, (unsigned)caller,
                      ba_live_total_get(), ba_script_live_get(), caller_script_live);
        if (result == BA_UID_RL_OVERFLOW && !__sync_lock_test_and_set(&ba_overflow_reported, 1))
            ba_dump("UID_RL_OVERFLOW", 1);
    } else if (threshold >= 0) {
        static const char *reasons[] = {"T=100", "T=200", "T=400", "T=800", "T=1200", "T=1600", "T=1900"};
        ba_dump(reasons[threshold], ba_thresholds[threshold] == 200);
    }
}

void ba_script_begin(void)
{
    uint32_t i;
    ba_lock(&ba_state_lock);
    ba_script_active = 1; ba_script_live = 0; ba_threshold_mask = 0;
    for (i = 0; i < BA_CALLER_CAP; ++i) {
        ba_callers[i].script_current = 0;
        ba_callers[i].script_highwater = 0;
        ba_callers[i].script_create_successes = 0;
        ba_callers[i].script_delete_successes = 0;
        ba_callers[i].script_create_failures = 0;
        ba_callers[i].script_delete_failures = 0;
        ba_callers[i].script_matched_deletes = 0;
    }
    ba_unlock(&ba_state_lock);
    ba_dump("M10_script_begin", 0);
}

void ba_snapshot(const char *milestone)
{
    int32_t family[BA_UID_FAMILY_COUNT], live, script;
    uint32_t unknown, i;
    if (__atomic_load_n(&ba_logging, __ATOMIC_ACQUIRE)) return;
    ba_lock(&ba_state_lock);
    live = ba_live_total; script = ba_script_live; unknown = ba_unknown_deletes;
    for (i = 0; i < BA_UID_FAMILY_COUNT; ++i) family[i] = ba_live_family[i];
    ba_unlock(&ba_state_lock);
    __atomic_store_n(&ba_logging, 1, __ATOMIC_RELEASE);
    vitaDiagLog("BA", "SNAP milestone=%s live=%d script_live=%d thread=%d sema=%d mutex=%d lwmutex=%d eventflag=%d msgpipe=%d memblock=%d unknown_delete=%u",
                milestone ? milestone : "-", live, script, family[BA_UID_THREAD],
                family[BA_UID_SEMA], family[BA_UID_MUTEX], family[BA_UID_LWMUTEX],
                family[BA_UID_EVENTFLAG], family[BA_UID_MSGPIPE], family[BA_UID_MEMBLOCK], unknown);
    __atomic_store_n(&ba_logging, 0, __ATOMIC_RELEASE);
}

void ba_dump_final(const char *reason) { ba_dump(reason, 1); }
int32_t ba_live_total_get(void) { return __atomic_load_n(&ba_live_total, __ATOMIC_RELAXED); }
int32_t ba_script_live_get(void) { return __atomic_load_n(&ba_script_live, __ATOMIC_RELAXED); }
uint32_t ba_unknown_delete_get(void) { return __atomic_load_n(&ba_unknown_deletes, __ATOMIC_RELAXED); }

#ifndef BA_HOST_TEST
int __real_sem_destroy(sem_t *sem);
int __wrap_sem_destroy(sem_t *sem)
{
    int value = INT32_MIN;
    int value_rc = sem_getvalue(sem, &value);
    void *object = sem ? (void *)*sem : NULL;
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int result = __real_sem_destroy(sem);
    int saved_errno = errno;
    uint32_t seq = __atomic_add_fetch(&ba_sem_destroy_calls, 1, __ATOMIC_RELAXED);
    if (result != 0)
        __atomic_add_fetch(&ba_sem_destroy_failures, 1, __ATOMIC_RELAXED);

    if (seq <= 12 || result != 0) {
        __atomic_store_n(&ba_logging, 1, __ATOMIC_RELEASE);
        vitaDiagLog("BA", "SEM_DESTROY seq=%u caller=0x%08lx slot=%p object=%p value_rc=%d value=%d result=%d errno=%d",
                    seq, (unsigned long)caller, (void *)sem, object,
                    value_rc, value, result, saved_errno);
        __atomic_store_n(&ba_logging, 0, __ATOMIC_RELEASE);
    }
    errno = saved_errno;
    return result;
}

int __real_pthread_cond_destroy(pthread_cond_t *cond);
int __wrap_pthread_cond_destroy(pthread_cond_t *cond)
{
    void *object = cond ? (void *)*cond : NULL;
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int result = __real_pthread_cond_destroy(cond);
    uint32_t seq = __atomic_add_fetch(&ba_cond_destroy_calls, 1, __ATOMIC_RELAXED);
    if (result != 0)
        __atomic_add_fetch(&ba_cond_destroy_failures, 1, __ATOMIC_RELAXED);

    if (seq <= 12 || result != 0) {
        __atomic_store_n(&ba_logging, 1, __ATOMIC_RELEASE);
        vitaDiagLog("BA", "COND_DESTROY seq=%u caller=0x%08lx slot=%p object=%p result=%d",
                    seq, (unsigned long)caller, (void *)cond, object, result);
        __atomic_store_n(&ba_logging, 0, __ATOMIC_RELEASE);
    }
    return result;
}

int __real_sem_init(sem_t *sem, int pshared, unsigned int value);
int __wrap_sem_init(sem_t *sem, int pshared, unsigned int value)
{
    int result = __real_sem_init(sem, pshared, value);
    __atomic_add_fetch(&ba_sem_init_calls, 1, __ATOMIC_RELAXED);
    if (result != 0)
        __atomic_add_fetch(&ba_sem_init_failures, 1, __ATOMIC_RELAXED);
    return result;
}

int __real_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int result = __real_pthread_mutex_init(mutex, attr);
    uint32_t seq = __atomic_add_fetch(&ba_mutex_init_calls, 1, __ATOMIC_RELAXED);
    if (result != 0)
        __atomic_add_fetch(&ba_mutex_init_failures, 1, __ATOMIC_RELAXED);
    if (seq <= 16 || (seq % 250u) == 0u || result != 0) {
        __atomic_store_n(&ba_logging, 1, __ATOMIC_RELEASE);
        vitaDiagLog("BA", "MUTEX_INIT seq=%u caller=0x%08lx slot=%p object=%p result=%d",
                    seq, (unsigned long)caller, (void *)mutex,
                    mutex ? (void *)*mutex : NULL, result);
        __atomic_store_n(&ba_logging, 0, __ATOMIC_RELEASE);
    }
    return result;
}

int __real_pthread_mutex_destroy(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    void *object = mutex ? (void *)*mutex : NULL;
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int result = __real_pthread_mutex_destroy(mutex);
    uint32_t seq = __atomic_add_fetch(&ba_mutex_destroy_calls, 1, __ATOMIC_RELAXED);
    if (result != 0)
        __atomic_add_fetch(&ba_mutex_destroy_failures, 1, __ATOMIC_RELAXED);
    if (seq <= 16 || (seq % 250u) == 0u || result != 0) {
        __atomic_store_n(&ba_logging, 1, __ATOMIC_RELEASE);
        vitaDiagLog("BA", "MUTEX_DESTROY seq=%u caller=0x%08lx slot=%p object=%p result=%d",
                    seq, (unsigned long)caller, (void *)mutex, object, result);
        __atomic_store_n(&ba_logging, 0, __ATOMIC_RELEASE);
    }
    return result;
}

int __real_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int __wrap_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    int result = __real_pthread_cond_init(cond, attr);
    __atomic_add_fetch(&ba_cond_init_calls, 1, __ATOMIC_RELAXED);
    if (result != 0)
        __atomic_add_fetch(&ba_cond_init_failures, 1, __ATOMIC_RELAXED);
    return result;
}
#endif

#define BA_CALLER_PC ((uintptr_t)__builtin_return_address(0))
#define BA_DELETE_WRAPPER(name, family, type) \
    int __real_##name(type); \
    int __wrap_##name(type uid) { \
        int r = __real_##name(uid); \
        if (!__atomic_load_n(&ba_logging, __ATOMIC_ACQUIRE)) \
            ba_record_delete(family, (uintptr_t)(uint32_t)uid, BA_CALLER_PC, r); \
        return r; \
    }

SceUID __real_sceKernelCreateSema(const char *, SceUInt, int, int, SceKernelSemaOptParam *);
SceUID __wrap_sceKernelCreateSema(const char *n, SceUInt a, int iv, int mv, SceKernelSemaOptParam *o)
{ SceUID r = __real_sceKernelCreateSema(n,a,iv,mv,o); ba_after_create("CreateSema",BA_UID_SEMA,(uintptr_t)(uint32_t)r,BA_CALLER_PC,r); return r; }
BA_DELETE_WRAPPER(sceKernelDeleteSema, BA_UID_SEMA, SceUID)

SceUID __real_sceKernelCreateMutex(const char *, SceUInt, int, SceKernelMutexOptParam *);
SceUID __wrap_sceKernelCreateMutex(const char *n, SceUInt a, int iv, SceKernelMutexOptParam *o)
{ SceUID r = __real_sceKernelCreateMutex(n,a,iv,o); ba_after_create("CreateMutex",BA_UID_MUTEX,(uintptr_t)(uint32_t)r,BA_CALLER_PC,r); return r; }
BA_DELETE_WRAPPER(sceKernelDeleteMutex, BA_UID_MUTEX, SceUID)

SceUID __real_sceKernelCreateThread(const char *, SceKernelThreadEntry, int, SceSize, SceUInt, int, const SceKernelThreadOptParam *);
SceUID __wrap_sceKernelCreateThread(const char *n, SceKernelThreadEntry e, int p, SceSize ss, SceUInt a, int cpu, const SceKernelThreadOptParam *o)
{ SceUID r = __real_sceKernelCreateThread(n,e,p,ss,a,cpu,o); ba_after_create("CreateThread",BA_UID_THREAD,(uintptr_t)(uint32_t)r,BA_CALLER_PC,r); return r; }
BA_DELETE_WRAPPER(sceKernelDeleteThread, BA_UID_THREAD, SceUID)

int __real_sceKernelCreateLwMutex(SceKernelLwMutexWork *, const char *, unsigned int, int, const SceKernelLwMutexOptParam *);
int __real_sceKernelDeleteLwMutex(SceKernelLwMutexWork *);
int __wrap_sceKernelCreateLwMutex(SceKernelLwMutexWork *w, const char *n, unsigned int a, int iv, const SceKernelLwMutexOptParam *o)
{ int r = __real_sceKernelCreateLwMutex(w,n,a,iv,o); ba_after_create("CreateLwMutex",BA_UID_LWMUTEX,(uintptr_t)w,BA_CALLER_PC,r); return r; }
int __wrap_sceKernelDeleteLwMutex(SceKernelLwMutexWork *w)
{ int r = __real_sceKernelDeleteLwMutex(w); if (!__atomic_load_n(&ba_logging,__ATOMIC_ACQUIRE)) ba_record_delete(BA_UID_LWMUTEX,(uintptr_t)w,BA_CALLER_PC,r); return r; }

SceUID __real_sceKernelCreateEventFlag(const char *, int, int, SceKernelEventFlagOptParam *);
SceUID __wrap_sceKernelCreateEventFlag(const char *n, int a, int b, SceKernelEventFlagOptParam *o)
{ SceUID r = __real_sceKernelCreateEventFlag(n,a,b,o); ba_after_create("CreateEventFlag",BA_UID_EVENTFLAG,(uintptr_t)(uint32_t)r,BA_CALLER_PC,r); return r; }
BA_DELETE_WRAPPER(sceKernelDeleteEventFlag, BA_UID_EVENTFLAG, int)

SceUID __real_sceKernelCreateMsgPipe(const char *, int, int, unsigned int, void *);
SceUID __wrap_sceKernelCreateMsgPipe(const char *n, int t, int a, unsigned int s, void *o)
{ SceUID r = __real_sceKernelCreateMsgPipe(n,t,a,s,o); ba_after_create("CreateMsgPipe",BA_UID_MSGPIPE,(uintptr_t)(uint32_t)r,BA_CALLER_PC,r); return r; }
BA_DELETE_WRAPPER(sceKernelDeleteMsgPipe, BA_UID_MSGPIPE, SceUID)

SceUID __real_sceKernelAllocMemBlock(const char *, SceKernelMemBlockType, SceSize, SceKernelAllocMemBlockOpt *);
SceUID __wrap_sceKernelAllocMemBlock(const char *n, SceKernelMemBlockType t, SceSize s, SceKernelAllocMemBlockOpt *o)
{ SceUID r = __real_sceKernelAllocMemBlock(n,t,s,o); ba_after_create("AllocMemBlock",BA_UID_MEMBLOCK,(uintptr_t)(uint32_t)r,BA_CALLER_PC,r); return r; }
BA_DELETE_WRAPPER(sceKernelFreeMemBlock, BA_UID_MEMBLOCK, SceUID)
