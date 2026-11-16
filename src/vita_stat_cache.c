/* Answers "does this file exist, how big is it" from folder listings held in
 * memory, for every stat() in the process (Ruby's File.exist?/file?/size,
 * PhysFS, SDL...). On the Vita each stat costs 1-20 ms, and a miss in a big
 * folder far more (430 ms in a 4200-file folder), so games that probe for
 * thousands of optional files at start (LonaRPG: ~17,000 checks) spent most
 * of their boot asking the memory card.
 *
 * How it stays correct:
 * - A folder is read in full (sceIoDopen/Dread) only once the time already
 *   spent on real stats there would have paid for reading it; until then
 *   stats go to the card as before. A folder the program enumerates itself
 *   (opendir/readdir to the end) is kept for free.
 * - Names are compared without ASCII case, as the card does. A name with
 *   other bytes that is not found asks the card (its case rules there are
 *   the file system's).
 * - Anything that changes a folder (open for writing, close, rename, mkdir,
 *   unlink, rmdir, utimes) drops what is held for it. While a file in a
 *   folder is open for writing, stats there go to the card.
 * - Results are converted exactly as newlib's _stat_r does.
 *
 * ux0:/data/hardrpg/statcache_off.flag turns it off (checked once). */
#include <errno.h>
#include <fcntl.h>
#include <reent.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>

extern char *__realpath(const char *path);
extern int __vita_sce_errno_to_errno(int sce, int def);

extern int __real__stat_r(struct _reent *, const char *, struct stat *);
extern int __real__close_r(struct _reent *, int);
extern int __real__unlink_r(struct _reent *, const char *);
extern int __real_rmdir(const char *);
extern int __real_utimes(const char *, const struct timeval *);
extern DIR *__real_opendir(const char *);
extern struct dirent *__real_readdir(DIR *);
extern int __real_closedir(DIR *);

void vitaFileNoteWrite(const char *path);
int vitaEngineOwnPath(const char *path);

/* ---------------------------------------------------------------- lock -- */

static atomic_flag lockFlag = ATOMIC_FLAG_INIT;

static void lock(void)
{
    unsigned spins = 0;
    while (atomic_flag_test_and_set_explicit(&lockFlag, memory_order_acquire))
    {
        if (++spins > 64)
            sceKernelDelayThread(200);
    }
}

static void unlock(void)
{
    atomic_flag_clear_explicit(&lockFlag, memory_order_release);
}

static uint64_t nowUs(void)
{
    return sceKernelGetProcessTimeWide();
}

/* --------------------------------------------------------------- tables -- */

typedef struct
{
    char *name;          /* lower-case ASCII; NULL = empty slot */
    uint32_t hash;
    SceMode mode;
    SceOff size;
    SceDateTime ctime, atime, mtime;
} Entry;

typedef struct Dir
{
    struct Dir *next;    /* hash chain */
    char *key;           /* lower-case absolute path, no trailing slash */
    uint32_t hash;
    Entry *slots;
    uint32_t cap, count;
    uint32_t epoch;      /* bumped whenever the folder may have changed */
    int complete;        /* entries hold every name in the folder */
    int writers;         /* files open for writing in it */
    uint32_t queries;
    uint64_t realUs;     /* time spent on real stats since the last change */
    uint64_t worstMissUs;
    int64_t estEntries;  /* -1 = not looked at yet, -2 = unknown */
    uint64_t lastUse;
} Dir;

#define DIR_BUCKETS 1024
static Dir *dirTable[DIR_BUCKETS];
static uint32_t totalEntries;
static uint64_t useClock;
static int state; /* 0 = unchecked, 1 = on, -1 = off */

/* Reading a folder costs about this much per name (measured on the card:
 * 4209 names in 5.7 s); refined from real reads. */
static uint64_t perEntryUs = 1350;
#define ENTRY_BUDGET 60000

static struct
{
    uint32_t hits, negHits, real, listed, listedEntries, captured, invalidations, missingDirs;
    uint64_t realUs, listUs;
} stats;

static uint32_t hashBytes(const char *s, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i)
        h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}

static char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int isAscii(const char *s)
{
    for (; *s; ++s)
        if ((unsigned char)*s >= 0x80)
            return 0;
    return 1;
}

static Dir *findDir(const char *key, size_t n, uint32_t h)
{
    for (Dir *d = dirTable[h & (DIR_BUCKETS - 1)]; d; d = d->next)
        if (d->hash == h && strlen(d->key) == n && !memcmp(d->key, key, n))
            return d;
    return NULL;
}

static void clearEntries(Dir *d)
{
    if (d->slots)
    {
        for (uint32_t i = 0; i < d->cap; ++i)
            free(d->slots[i].name);
        free(d->slots);
    }
    totalEntries -= d->count;
    d->slots = NULL;
    d->cap = d->count = 0;
    d->complete = 0;
}

static Dir *getDir(const char *key, size_t n, uint32_t h)
{
    Dir *d = findDir(key, n, h);
    if (d)
        return d;
    d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    d->key = malloc(n + 1);
    if (!d->key)
    {
        free(d);
        return NULL;
    }
    memcpy(d->key, key, n);
    d->key[n] = 0;
    d->hash = h;
    d->estEntries = -1;
    d->next = dirTable[h & (DIR_BUCKETS - 1)];
    dirTable[h & (DIR_BUCKETS - 1)] = d;
    return d;
}

static Entry *lookup(Dir *d, const char *lname, uint32_t h)
{
    if (!d->cap)
        return NULL;
    for (uint32_t i = h & (d->cap - 1);; i = (i + 1) & (d->cap - 1))
    {
        Entry *e = &d->slots[i];
        if (!e->name)
            return NULL;
        if (e->hash == h && !strcmp(e->name, lname))
            return e;
    }
}

static void evictIfNeeded(void);

static void insert(Dir *d, const char *name, const SceIoStat *st)
{
    size_t n = strlen(name);
    if (!n || n > 255)
        return;
    char lname[256];
    for (size_t i = 0; i < n; ++i)
        lname[i] = lowerAscii(name[i]);
    lname[n] = 0;
    uint32_t h = hashBytes(lname, n);
    if ((d->count + 1) * 4 > d->cap * 3)
    {
        uint32_t ncap = d->cap ? d->cap * 2 : 16;
        Entry *ns = calloc(ncap, sizeof(Entry));
        if (!ns)
            return;
        for (uint32_t i = 0; i < d->cap; ++i)
        {
            Entry *e = &d->slots[i];
            if (!e->name)
                continue;
            uint32_t j = e->hash & (ncap - 1);
            while (ns[j].name)
                j = (j + 1) & (ncap - 1);
            ns[j] = *e;
        }
        free(d->slots);
        d->slots = ns;
        d->cap = ncap;
    }
    Entry *e = lookup(d, lname, h);
    if (!e)
    {
        uint32_t j = h & (d->cap - 1);
        while (d->slots[j].name)
            j = (j + 1) & (d->cap - 1);
        e = &d->slots[j];
        e->name = malloc(n + 1);
        if (!e->name)
            return;
        memcpy(e->name, lname, n + 1);
        e->hash = h;
        d->count++;
        totalEntries++;
    }
    e->mode = st->st_mode;
    e->size = st->st_size;
    e->ctime = st->st_ctime;
    e->atime = st->st_atime;
    e->mtime = st->st_mtime;
}

/* What newlib's scestat_to_stat does. */
static void toStat(const Entry *e, struct stat *out)
{
    memset(out, 0, sizeof(*out));
    out->st_size = e->size;
    if ((e->mode & 0xf000) == SCE_S_IFREG)
        out->st_mode |= S_IFREG;
    if ((e->mode & 0xf000) == SCE_S_IFDIR)
        out->st_mode |= S_IFDIR;
    time_t t;
    sceRtcGetTime_t(&e->atime, &t);
    out->st_atime = t;
    sceRtcGetTime_t(&e->mtime, &t);
    out->st_mtime = t;
    sceRtcGetTime_t(&e->ctime, &t);
    out->st_ctime = t;
}

static void invalidateDir(Dir *d)
{
    clearEntries(d);
    d->epoch++;
    d->queries = 0;
    d->realUs = 0;
    d->estEntries = -1;
    stats.invalidations++;
}

/* Splits an absolute path into its folder and last name. Returns 0 when the
 * path has no folder part this cache handles (a device root). */
static int splitPath(const char *p, size_t *dirLen, const char **name)
{
    const char *slash = strrchr(p, '/');
    const char *colon = strchr(p, ':');
    if (!slash || !colon || slash < colon || slash[1] == 0)
        return 0;
    /* "ux0:/x" -> folder "ux0:" is the device root: not handled. */
    if (slash == colon + 1)
        return 0;
    *dirLen = (size_t)(slash - p);
    *name = slash + 1;
    return 1;
}

/* Drops what is held for the folder holding `path` (and, for a folder that
 * was renamed or removed, everything under it). Call with the lock held. */
static void changedLocked(const char *abs, int subtree)
{
    size_t n = strlen(abs);
    while (n > 1 && abs[n - 1] == '/')
        n--;
    char key[1024];
    if (n >= sizeof(key))
    {
        /* Too long to name: forget everything. */
        for (unsigned b = 0; b < DIR_BUCKETS; ++b)
            for (Dir *d = dirTable[b]; d; d = d->next)
                invalidateDir(d);
        return;
    }
    for (size_t i = 0; i < n; ++i)
        key[i] = lowerAscii(abs[i]);
    key[n] = 0;
    size_t dl;
    const char *name;
    if (splitPath(key, &dl, &name))
    {
        Dir *d = findDir(key, dl, hashBytes(key, dl));
        if (d)
            invalidateDir(d);
    }
    if (subtree)
    {
        for (unsigned b = 0; b < DIR_BUCKETS; ++b)
            for (Dir *d = dirTable[b]; d; d = d->next)
                if (!strncmp(d->key, key, n) && (d->key[n] == 0 || d->key[n] == '/'))
                    invalidateDir(d);
    }
}

static int enabled(void)
{
    if (state == 0)
    {
        SceIoStat st;
        state = sceIoGetstat("ux0:/data/hardrpg/statcache_off.flag", &st) >= 0 ? -1 : 1;
    }
    return state > 0;
}

static void changed(const char *path, int subtree)
{
    if (!path)
        return;
    char *abs = __realpath(path);
    lock();
    if (abs)
        changedLocked(abs, subtree);
    else
    {
        for (unsigned b = 0; b < DIR_BUCKETS; ++b)
            for (Dir *d = dirTable[b]; d; d = d->next)
                invalidateDir(d);
    }
    unlock();
    free(abs);
}

/* ----------------------------------------------------- folder reading -- */

/* Reads the whole folder. Lock held. */
static void listDir(Dir *d)
{
    uint64_t t0 = nowUs();
    clearEntries(d);
    SceUID fd = sceIoDopen(d->key);
    if (fd < 0)
    {
        /* No such folder (or a file): nothing can be found inside it until
         * a mkdir or rename there drops this. */
        if (fd == (SceUID)0x80010002 || fd == (SceUID)0x80010014)
        {
            d->complete = 1;
            stats.missingDirs++;
        }
        return;
    }
    SceIoDirent ent;
    int ok = 1;
    for (;;)
    {
        memset(&ent, 0, sizeof(ent));
        int r = sceIoDread(fd, &ent);
        if (r == 0)
            break;
        if (r < 0)
        {
            ok = 0;
            break;
        }
        insert(d, ent.d_name, &ent.d_stat);
    }
    sceIoDclose(fd);
    uint64_t dt = nowUs() - t0;
    stats.listUs += dt;
    if (!ok)
    {
        clearEntries(d);
        return;
    }
    d->complete = 1;
    stats.listed++;
    stats.listedEntries += d->count;
    if (d->count >= 32)
    {
        uint64_t per = dt / d->count;
        perEntryUs = (perEntryUs * 3 + per) / 4;
        if (perEntryUs < 50)
            perEntryUs = 50;
    }
    evictIfNeeded();
}

static void evictIfNeeded(void)
{
    while (totalEntries > ENTRY_BUDGET)
    {
        Dir *oldest = NULL;
        for (unsigned b = 0; b < DIR_BUCKETS; ++b)
            for (Dir *d = dirTable[b]; d; d = d->next)
                if (d->count && (!oldest || d->lastUse < oldest->lastUse))
                    oldest = d;
        if (!oldest)
            return;
        invalidateDir(oldest);
    }
}

/* Whether reading the whole folder now pays for itself: once the real stats
 * already spent there cost as much as a read (rent-or-buy), or right away
 * for a small folder. */
static int worthListing(Dir *d)
{
    int64_t entries = d->estEntries;
    if (entries < 0 && d->worstMissUs)
        entries = (int64_t)(d->worstMissUs / 100); /* a miss scans the folder */
    if (entries < 0)
        return d->realUs >= 2000000;
    uint64_t cost = (uint64_t)(entries + 1) * perEntryUs;
    if (cost <= 30000 && d->queries >= 2)
        return 1;
    return d->realUs >= cost;
}

/* Folder size from its own stat (exFAT: 32-byte records, about three per
 * name). Lock not held. */
static int64_t estimateEntries(const char *dirPath)
{
    SceIoStat st;
    memset(&st, 0, sizeof(st));
    if (sceIoGetstat(dirPath, &st) < 0 || st.st_size <= 0)
        return -2;
    return (int64_t)(st.st_size / 96);
}

/* Lock held. 1 = the folder `key` is known not to exist (an ancestor that is
 * held in full lacks it). */
static int knownMissing(const char *key, size_t n)
{
    char buf[1024];
    if (n >= sizeof(buf))
        return 0;
    memcpy(buf, key, n);
    buf[n] = 0;
    for (;;)
    {
        size_t dl;
        const char *name;
        if (!splitPath(buf, &dl, &name))
            return 0;
        Dir *p = findDir(buf, dl, hashBytes(buf, dl));
        if (p && p->complete && !p->writers && isAscii(name))
        {
            Entry *e = lookup(p, name, hashBytes(name, strlen(name)));
            if (!e)
                return 1;
            return (e->mode & 0xf000) != SCE_S_IFDIR;
        }
        buf[dl] = 0;
    }
}

/* ------------------------------------------------------------------ stat -- */

int __wrap__stat_r(struct _reent *r, const char *path, struct stat *out)
{
    if (!enabled() || !path)
        return __real__stat_r(r, path, out);
    char *abs = __realpath(path);
    if (!abs)
        return __real__stat_r(r, path, out);
    if (vitaEngineOwnPath(abs))
    {
        /* Logs and reports are also written with sceIo calls directly. */
        free(abs);
        return __real__stat_r(r, path, out);
    }

    size_t len = strlen(abs);
    size_t dl;
    const char *name;
    char key[1024];
    if (len >= sizeof(key) || !splitPath(abs, &dl, &name))
    {
        free(abs);
        return __real__stat_r(r, path, out);
    }
    for (size_t i = 0; i <= len; ++i)
        key[i] = lowerAscii(abs[i]);
    const char *lname = key + dl + 1;
    uint32_t dh = hashBytes(key, dl);
    uint32_t nh = hashBytes(lname, len - dl - 1);
    int ascii = isAscii(lname);

    lock();
    Dir *d = findDir(key, dl, dh);
    if (d && !d->writers)
    {
        d->lastUse = ++useClock;
        Entry *e = lookup(d, lname, nh);
        if (e)
        {
            toStat(e, out);
            stats.hits++;
            unlock();
            free(abs);
            r->_errno = 0;
            return 0;
        }
        if (d->complete && ascii)
        {
            stats.negHits++;
            unlock();
            free(abs);
            r->_errno = ENOENT;
            return -1;
        }
    }
    if ((!d || !d->writers) && knownMissing(key, dl))
    {
        stats.negHits++;
        unlock();
        free(abs);
        r->_errno = ENOENT;
        return -1;
    }
    int64_t est = -1;
    int needEstimate = !d || d->estEntries == -1;
    if (needEstimate)
    {
        /* The folder's own size, from its parent when that is held. */
        size_t pl;
        const char *pname;
        char pkey[1024];
        memcpy(pkey, key, dl);
        pkey[dl] = 0;
        if (splitPath(pkey, &pl, &pname))
        {
            Dir *p = findDir(pkey, pl, hashBytes(pkey, pl));
            Entry *pe = p ? lookup(p, pname, hashBytes(pname, strlen(pname))) : NULL;
            if (pe)
            {
                est = pe->size > 0 ? (int64_t)(pe->size / 96) : -2;
                needEstimate = 0;
            }
        }
    }
    unlock();

    if (needEstimate)
    {
        char dirPath[1024];
        memcpy(dirPath, abs, dl);
        dirPath[dl] = 0;
        est = estimateEntries(dirPath);
    }

    /* The real stat, as newlib does it. */
    SceIoStat sst;
    memset(&sst, 0, sizeof(sst));
    uint64_t t0 = nowUs();
    int ret = sceIoGetstat(abs, &sst);
    uint64_t dt = nowUs() - t0;

    lock();
    stats.real++;
    stats.realUs += dt;
    d = getDir(key, dl, dh);
    if (d && !d->writers)
    {
        d->lastUse = ++useClock;
        if (est != -1 && d->estEntries == -1)
            d->estEntries = est;
        d->queries++;
        d->realUs += dt;
        if (ret < 0 && dt > d->worstMissUs)
            d->worstMissUs = dt;
        if (ret >= 0 && ascii)
            insert(d, name, &sst);
        if (!d->complete && worthListing(d))
            listDir(d);
    }
    unlock();
    free(abs);

    if (ret < 0)
    {
        r->_errno = __vita_sce_errno_to_errno(ret, 0);
        return -1;
    }
    Entry tmp;
    tmp.mode = sst.st_mode;
    tmp.size = sst.st_size;
    tmp.ctime = sst.st_ctime;
    tmp.atime = sst.st_atime;
    tmp.mtime = sst.st_mtime;
    toStat(&tmp, out);
    r->_errno = 0;
    return 0;
}

/* -------------------------------------------------------- write tracking -- */

#define MAX_WRITERS 64
static struct
{
    int fd;
    Dir *dir;
} writers[MAX_WRITERS];

/* Called by the _open_r wrapper before a write open. Returns the folder to
 * hand to vitaStatCacheOpened. */
void *vitaStatCacheBeginWrite(const char *path)
{
    if (!path)
        return NULL;
    char *abs = __realpath(path);
    if (!abs)
    {
        changed(path, 0);
        return NULL;
    }
    void *res = NULL;
    size_t len = strlen(abs);
    size_t dl;
    const char *name;
    lock();
    if (len < 1024 && splitPath(abs, &dl, &name))
    {
        char key[1024];
        for (size_t i = 0; i < dl; ++i)
            key[i] = lowerAscii(abs[i]);
        key[dl] = 0;
        Dir *d = getDir(key, dl, hashBytes(key, dl));
        if (d)
        {
            invalidateDir(d);
            d->writers++;
            res = d;
        }
    }
    else
        changedLocked(abs, 0);
    unlock();
    free(abs);
    return res;
}

/* After the open: remember the descriptor until it is closed, or release the
 * folder if the open failed. */
void vitaStatCacheOpened(void *dir, int fd)
{
    if (!dir)
        return;
    Dir *d = dir;
    lock();
    int kept = 0;
    if (fd >= 0)
    {
        for (int i = 0; i < MAX_WRITERS; ++i)
            if (!writers[i].dir)
            {
                writers[i].fd = fd;
                writers[i].dir = d;
                kept = 1;
                break;
            }
    }
    /* A failed open releases the folder. One too many to track keeps it
     * counted as written for good: never cached again, which is safe. */
    if (!kept && fd < 0 && d->writers > 0)
        d->writers--;
    invalidateDir(d);
    unlock();
}

int __wrap__close_r(struct _reent *r, int fd)
{
    int res = __real__close_r(r, fd);
    lock();
    for (int i = 0; i < MAX_WRITERS; ++i)
        if (writers[i].dir && writers[i].fd == fd)
        {
            Dir *d = writers[i].dir;
            writers[i].dir = NULL;
            if (d->writers > 0)
                d->writers--;
            invalidateDir(d);
            break;
        }
    unlock();
    return res;
}

void vitaStatCacheChanged(const char *path, int subtree)
{
    changed(path, subtree);
}

int __wrap__unlink_r(struct _reent *r, const char *path)
{
    int res = __real__unlink_r(r, path);
    changed(path, 0);
    vitaFileNoteWrite(path);
    return res;
}

int __wrap_rmdir(const char *path)
{
    int res = __real_rmdir(path);
    changed(path, 1);
    vitaFileNoteWrite(path);
    return res;
}

int __wrap_utimes(const char *path, const struct timeval *tv)
{
    int res = __real_utimes(path, tv);
    changed(path, 0);
    return res;
}

/* ----------------------------------------------- program's own listings -- */

#define MAX_ENUMS 16
static struct
{
    DIR *dir;
    Dir *rec;
    uint32_t epoch;
    int failed;
} enums[MAX_ENUMS];

DIR *__wrap_opendir(const char *path)
{
    DIR *dp = __real_opendir(path);
    if (!dp || !enabled())
        return dp;
    char *abs = __realpath(path);
    if (!abs)
        return dp;
    size_t n = strlen(abs);
    while (n > 1 && abs[n - 1] == '/')
        n--;
    if (n < 1024 && strchr(abs, ':') && strchr(abs, '/') && strchr(abs, '/') < abs + n)
    {
        char key[1024];
        for (size_t i = 0; i < n; ++i)
            key[i] = lowerAscii(abs[i]);
        key[n] = 0;
        lock();
        Dir *d = getDir(key, n, hashBytes(key, n));
        if (d && !d->writers)
        {
            for (int i = 0; i < MAX_ENUMS; ++i)
                if (!enums[i].dir)
                {
                    enums[i].dir = dp;
                    enums[i].rec = d;
                    enums[i].epoch = d->epoch;
                    enums[i].failed = 0;
                    break;
                }
        }
        unlock();
    }
    free(abs);
    return dp;
}

struct dirent *__wrap_readdir(DIR *dp)
{
    int saved = errno;
    errno = 0;
    struct dirent *e = __real_readdir(dp);
    int err = errno;
    if (!err)
        errno = saved;
    lock();
    for (int i = 0; i < MAX_ENUMS; ++i)
    {
        if (enums[i].dir != dp)
            continue;
        Dir *d = enums[i].rec;
        if (d->epoch != enums[i].epoch || d->writers || enums[i].failed)
            break;
        if (e)
        {
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
                insert(d, e->d_name, &e->d_stat);
        }
        else if (err)
            enums[i].failed = 1;
        else if (!d->complete)
        {
            d->complete = 1;
            stats.captured++;
            evictIfNeeded();
        }
        d->lastUse = ++useClock;
        break;
    }
    unlock();
    return e;
}

int __wrap_closedir(DIR *dp)
{
    lock();
    for (int i = 0; i < MAX_ENUMS; ++i)
        if (enums[i].dir == dp)
            enums[i].dir = NULL;
    unlock();
    return __real_closedir(dp);
}

/* For probes: one line of counters. */
int vitaStatCacheStats(char *buf, int size)
{
    lock();
    unsigned dirs = 0, complete = 0;
    for (unsigned b = 0; b < DIR_BUCKETS; ++b)
        for (Dir *d = dirTable[b]; d; d = d->next)
        {
            dirs++;
            complete += d->complete != 0;
        }
    int n = snprintf(buf, size,
                     "on=%d hits=%u neg=%u real=%u real_ms=%u listed=%u listed_entries=%u list_ms=%u "
                     "captured=%u missing_dirs=%u inval=%u dirs=%u complete=%u entries=%u per_entry_us=%u",
                     state > 0, stats.hits, stats.negHits, stats.real, (unsigned)(stats.realUs / 1000),
                     stats.listed, stats.listedEntries, (unsigned)(stats.listUs / 1000), stats.captured,
                     stats.missingDirs, stats.invalidations, dirs, complete, totalEntries, (unsigned)perEntryUs);
    unlock();
    return n;
}
