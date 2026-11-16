/* Boot pack: the small files a game reads while it starts, kept in one file.
 *
 * Games like LonaRPG open and read thousands of small files at every start
 * (scripts, JSON configs, images). On the Vita each open+read costs ~5 ms, so
 * that alone was most of a two-minute boot.
 *
 * First start: every file under the game folder that is opened read-only, is
 * at most MEMBER_MAX bytes and gets read through from the start is copied, in
 * the order the game reads it, into ux0:/data/hardrpg/cache/bootpack-v1/
 * <game>.pack. Once nothing new has been read for IDLE_US (the game is at its
 * title), the pack is closed and named.
 *
 * Next starts: such an open is answered from the pack when the file's size
 * and modification time (from stat, which the stat cache answers from memory)
 * still match what was recorded. The descriptor handed out is a dup() of the
 * pack's own one, so it is a real descriptor to every other call; reads,
 * seeks and fstat on it are answered from the pack, read ahead in big chunks
 * since the game asks for the files in the same order again. A file that no
 * longer matches is read from the card, and the pack is dropped so the next
 * start records a fresh one.
 *
 * ux0:/data/hardrpg/bootpack_off.flag turns it off. */
#include <errno.h>
#include <fcntl.h>
#include <reent.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

extern char *__realpath(const char *path);
extern int __real__read_r(struct _reent *, int, void *, size_t);
extern _off_t __real__lseek_r(struct _reent *, int, _off_t, int);
extern int __real__fstat_r(struct _reent *, int, struct stat *);

#define PACK_DIR "ux0:/data/hardrpg/cache/bootpack-v1"
#define MEMBER_MAX (256 * 1024)
#define PACK_BUDGET (40u * 1024 * 1024)
#define MEMBERS_MAX 12000
#define IDLE_US 15000000ull
#define WINDOW (1024 * 1024)
#define WRITE_CHUNK (256 * 1024)
#define MAX_FDS 256
#define MAGIC 0x50425248u /* "HRBP" */
#define VERSION 1

typedef struct
{
    char *path; /* lower-case absolute */
    uint32_t hash;
    uint32_t size, offset;
    int32_t atime, mtime, ctime;
} Member;

enum { FD_NONE, FD_SERVED, FD_RECORD };

typedef struct
{
    int kind;
    /* served */
    Member *m;
    int64_t pos;
    /* recording */
    char *path;
    uint32_t size;
    int32_t atime, mtime, ctime;
    uint8_t *buf;
    uint32_t len;
    int64_t rpos;
} FdState;

static atomic_flag lockFlag = ATOMIC_FLAG_INIT;
static int mode; /* 0 off, 1 recording, 2 serving */
static char gameRoot[512];
static size_t gameRootLen;
static char packPath[600], partPath[620];

static Member *members;
static uint32_t memberCount, memberCap;
static uint32_t *slots; /* hash set: index+1 into members, 0 = empty */
static uint32_t slotCap;
static FdState fds[MAX_FDS];

/* recording */
static SceUID partUid = -1;
static uint8_t *wbuf;
static uint32_t wlen, packBytes;
static uint64_t lastActivity;

/* serving */
static SceUID packUid = -1;
static int packFd = -1;
static uint8_t *window;
static uint32_t winStart, winLen;
static uint32_t served, stale, missed;
static int reported;

static void lock(void)
{
    unsigned spins = 0;
    while (atomic_flag_test_and_set_explicit(&lockFlag, memory_order_acquire))
        if (++spins > 64)
            sceKernelDelayThread(200);
}

static void unlock(void)
{
    atomic_flag_clear_explicit(&lockFlag, memory_order_release);
}

static uint32_t hashStr(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s)
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

static void lowerInPlace(char *s)
{
    for (; *s; ++s)
        if (*s >= 'A' && *s <= 'Z')
            *s += 32;
}

static Member *findMember(const char *lpath, uint32_t h)
{
    if (!slotCap)
        return NULL;
    for (uint32_t i = h & (slotCap - 1);; i = (i + 1) & (slotCap - 1))
    {
        if (!slots[i])
            return NULL;
        Member *m = &members[slots[i] - 1];
        if (m->hash == h && !strcmp(m->path, lpath))
            return m;
    }
}

static int addMember(const Member *src)
{
    if (memberCount == memberCap)
    {
        uint32_t ncap = memberCap ? memberCap * 2 : 1024;
        Member *n = realloc(members, ncap * sizeof(Member));
        if (!n)
            return 0;
        members = n;
        memberCap = ncap;
    }
    if ((memberCount + 1) * 2 > slotCap)
    {
        uint32_t ncap = slotCap ? slotCap * 2 : 4096;
        uint32_t *ns = calloc(ncap, sizeof(uint32_t));
        if (!ns)
            return 0;
        for (uint32_t k = 0; k < memberCount; ++k)
        {
            uint32_t i = members[k].hash & (ncap - 1);
            while (ns[i])
                i = (i + 1) & (ncap - 1);
            ns[i] = k + 1;
        }
        free(slots);
        slots = ns;
        slotCap = ncap;
    }
    members[memberCount] = *src;
    uint32_t i = src->hash & (slotCap - 1);
    while (slots[i])
        i = (i + 1) & (slotCap - 1);
    slots[i] = ++memberCount;
    return 1;
}

/* Lower-case absolute path of a game file, or NULL. */
static char *gamePath(const char *path)
{
    if (!path || !gameRootLen)
        return NULL;
    char *abs = __realpath(path);
    if (!abs)
        return NULL;
    lowerInPlace(abs);
    if (strncmp(abs, gameRoot, gameRootLen) != 0 || abs[gameRootLen] != '/')
    {
        free(abs);
        return NULL;
    }
    return abs;
}

/* ------------------------------------------------------------ loading -- */

static int readAll(SceUID uid, uint32_t off, void *dst, uint32_t n)
{
    uint8_t *p = dst;
    while (n)
    {
        int r = sceIoPread(uid, p, n, off);
        if (r <= 0)
            return 0;
        p += r;
        off += r;
        n -= r;
    }
    return 1;
}

static int loadPack(void)
{
    SceUID uid = sceIoOpen(packPath, SCE_O_RDONLY, 0);
    if (uid < 0)
        return 0;
    uint32_t trailer[6];
    SceOff end = sceIoLseek(uid, 0, SCE_SEEK_END);
    if (end < (SceOff)sizeof(trailer) || !readAll(uid, (uint32_t)end - sizeof(trailer), trailer, sizeof(trailer)) ||
        trailer[0] != MAGIC || trailer[1] != VERSION || trailer[5] != hashStr(gameRoot) ||
        (uint64_t)trailer[3] + trailer[4] + sizeof(trailer) != (uint64_t)end)
    {
        sceIoClose(uid);
        sceIoRemove(packPath);
        return 0;
    }
    uint32_t count = trailer[2], ioff = trailer[3], isize = trailer[4];
    uint8_t *index = malloc(isize);
    if (!index || !readAll(uid, ioff, index, isize))
    {
        free(index);
        sceIoClose(uid);
        return 0;
    }
    uint8_t *p = index, *e = index + isize;
    for (uint32_t k = 0; k < count; ++k)
    {
        uint32_t rec[6];
        if (p + 4 > e)
            break;
        uint32_t plen;
        memcpy(&plen, p, 4);
        p += 4;
        if (p + plen + sizeof(rec) > e)
            break;
        Member m;
        m.path = malloc(plen + 1);
        if (!m.path)
            break;
        memcpy(m.path, p, plen);
        m.path[plen] = 0;
        p += plen;
        memcpy(rec, p, sizeof(rec));
        p += sizeof(rec);
        m.size = rec[0];
        m.offset = rec[1];
        m.atime = (int32_t)rec[2];
        m.mtime = (int32_t)rec[3];
        m.ctime = (int32_t)rec[4];
        m.hash = hashStr(m.path);
        if (!addMember(&m))
        {
            free(m.path);
            break;
        }
    }
    free(index);
    packUid = uid;
    window = malloc(WINDOW);
    packFd = open(packPath, O_RDONLY);
    if (!window || packFd < 0)
    {
        sceIoClose(uid);
        packUid = -1;
        if (packFd >= 0)
            close(packFd);
        packFd = -1;
        return 0;
    }
    return 1;
}

void vitaBootPackStart(const char *gameFolder)
{
    SceIoStat st;
    if (mode || !gameFolder || !*gameFolder || sceIoGetstat("ux0:/data/hardrpg/bootpack_off.flag", &st) >= 0)
        return;
    char *abs = __realpath(gameFolder);
    if (!abs)
        return;
    size_t n = strlen(abs);
    while (n > 1 && abs[n - 1] == '/')
        n--;
    if (n >= sizeof(gameRoot))
    {
        free(abs);
        return;
    }
    memcpy(gameRoot, abs, n);
    gameRoot[n] = 0;
    free(abs);
    lowerInPlace(gameRoot);
    gameRootLen = n;
    snprintf(packPath, sizeof(packPath), PACK_DIR "/%08x.pack", (unsigned)hashStr(gameRoot));
    snprintf(partPath, sizeof(partPath), "%s.part", packPath);
    sceIoMkdir("ux0:/data/hardrpg/cache", 0777);
    sceIoMkdir(PACK_DIR, 0777);

    lock();
    if (loadPack())
    {
        mode = 2;
        lastActivity = sceKernelGetProcessTimeWide();
        fprintf(stderr, "Boot pack: %u files\n", (unsigned)memberCount);
    }
    else
    {
        partUid = sceIoOpen(partPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        wbuf = malloc(WRITE_CHUNK);
        if (partUid >= 0 && wbuf)
        {
            mode = 1;
            lastActivity = sceKernelGetProcessTimeWide();
            fprintf(stderr, "Boot pack: recording\n");
        }
        else
        {
            if (partUid >= 0)
                sceIoClose(partUid);
            partUid = -1;
            free(wbuf);
            wbuf = NULL;
        }
    }
    unlock();
}

/* ---------------------------------------------------------- recording -- */

static int flushWrites(void)
{
    if (!wlen)
        return 1;
    int ok = sceIoWrite(partUid, wbuf, wlen) == (int)wlen;
    wlen = 0;
    return ok;
}

static void stopRecording(int keep);

static void commit(FdState *f)
{
    Member m;
    m.path = f->path;
    m.hash = hashStr(f->path);
    m.size = f->size;
    m.offset = packBytes;
    m.atime = f->atime;
    m.mtime = f->mtime;
    m.ctime = f->ctime;
    int ok = !findMember(m.path, m.hash) && memberCount < MEMBERS_MAX && packBytes + f->size <= PACK_BUDGET;
    if (ok)
    {
        uint32_t left = f->size;
        const uint8_t *src = f->buf;
        while (left && ok)
        {
            uint32_t n = WRITE_CHUNK - wlen < left ? WRITE_CHUNK - wlen : left;
            memcpy(wbuf + wlen, src, n);
            wlen += n;
            src += n;
            left -= n;
            if (wlen == WRITE_CHUNK)
                ok = flushWrites();
        }
        if (ok && addMember(&m))
        {
            packBytes += f->size;
            f->path = NULL; /* now owned by the member */
            lastActivity = sceKernelGetProcessTimeWide();
        }
        else if (!ok)
            stopRecording(0);
    }
    free(f->path);
    free(f->buf);
    memset(f, 0, sizeof(*f));
}

static void stopRecording(int keep)
{
    for (int i = 0; i < MAX_FDS; ++i)
        if (fds[i].kind == FD_RECORD)
        {
            free(fds[i].path);
            free(fds[i].buf);
            memset(&fds[i], 0, sizeof(fds[i]));
        }
    int ok = keep && memberCount && flushWrites();
    if (ok)
    {
        /* Index, then the trailer. */
        uint32_t ioff = packBytes, isize = 0;
        for (uint32_t k = 0; k < memberCount && ok; ++k)
        {
            Member *m = &members[k];
            uint32_t plen = strlen(m->path);
            uint32_t rec[6] = {m->size, m->offset, (uint32_t)m->atime, (uint32_t)m->mtime, (uint32_t)m->ctime, 0};
            if (wlen + 4 + plen + sizeof(rec) > WRITE_CHUNK)
                ok = flushWrites();
            memcpy(wbuf + wlen, &plen, 4);
            memcpy(wbuf + wlen + 4, m->path, plen);
            memcpy(wbuf + wlen + 4 + plen, rec, sizeof(rec));
            wlen += 4 + plen + sizeof(rec);
            isize += 4 + plen + sizeof(rec);
        }
        uint32_t trailer[6] = {MAGIC, VERSION, memberCount, ioff, isize, hashStr(gameRoot)};
        if (ok && wlen + sizeof(trailer) > WRITE_CHUNK)
            ok = flushWrites();
        memcpy(wbuf + wlen, trailer, sizeof(trailer));
        wlen += sizeof(trailer);
        ok = ok && flushWrites();
    }
    sceIoClose(partUid);
    partUid = -1;
    if (ok)
    {
        sceIoRemove(packPath);
        ok = sceIoRename(partPath, packPath) >= 0;
    }
    if (!ok)
        sceIoRemove(partPath);
    fprintf(stderr, "Boot pack: %s (%u files, %u KB)\n", ok ? "saved" : "not saved", (unsigned)memberCount,
            (unsigned)(packBytes / 1024));
    free(wbuf);
    wbuf = NULL;
    mode = 0;
}

/* --------------------------------------------------------------- hooks -- */

static int readOnly(int flags)
{
    return (flags & O_ACCMODE) == O_RDONLY && !(flags & (O_CREAT | O_TRUNC | O_APPEND));
}

/* Before a real open: a descriptor answered from the pack, or -1. */
int vitaBootPackOpen(struct _reent *r, const char *path, int flags)
{
    if (mode != 2 || !readOnly(flags))
        return -1;
    char *lpath = gamePath(path);
    if (!lpath)
        return -1;
    lock();
    Member *m = mode == 2 ? findMember(lpath, hashStr(lpath)) : NULL;
    unlock();
    int fd = -1;
    if (m)
    {
        struct stat st;
        if (stat(path, &st) == 0 && (uint32_t)st.st_size == m->size && (int32_t)st.st_mtime == m->mtime)
        {
            fd = dup(packFd);
            if (fd >= 0 && fd < MAX_FDS)
            {
                lock();
                fds[fd].kind = FD_SERVED;
                fds[fd].m = m;
                fds[fd].pos = 0;
                served++;
                lastActivity = sceKernelGetProcessTimeWide();
                unlock();
                r->_errno = 0;
            }
            else if (fd >= 0)
            {
                close(fd);
                fd = -1;
            }
        }
        else
            stale++;
    }
    else
        missed++;
    free(lpath);
    return fd;
}

/* After a real open: start recording a file the pack may want. */
void vitaBootPackOpened(const char *path, int flags, int fd)
{
    if (mode != 1 || fd < 0 || fd >= MAX_FDS || !readOnly(flags))
        return;
    char *lpath = gamePath(path);
    if (!lpath)
        return;
    struct stat st;
    struct _reent *r = _REENT;
    if (__real__fstat_r(r, fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > MEMBER_MAX)
    {
        free(lpath);
        return;
    }
    lock();
    if (mode == 1 && !findMember(lpath, hashStr(lpath)))
    {
        FdState *f = &fds[fd];
        if (f->kind == FD_RECORD)
        {
            free(f->path);
            free(f->buf);
        }
        memset(f, 0, sizeof(*f));
        f->kind = FD_RECORD;
        f->path = lpath;
        lpath = NULL;
        f->size = (uint32_t)st.st_size;
        f->atime = (int32_t)st.st_atime;
        f->mtime = (int32_t)st.st_mtime;
        f->ctime = (int32_t)st.st_ctime;
    }
    unlock();
    free(lpath);
}

/* Lock held. Copies from the pack at off, reading ahead as needed. */
static int copyFromPack(uint32_t off, uint8_t *dst, uint32_t n)
{
    while (n)
    {
        if (off < winStart || off >= winStart + winLen)
        {
            int r = sceIoPread(packUid, window, WINDOW, off);
            if (r <= 0)
                return 0;
            winStart = off;
            winLen = (uint32_t)r;
        }
        uint32_t avail = winStart + winLen - off;
        uint32_t k = n < avail ? n : avail;
        memcpy(dst, window + (off - winStart), k);
        dst += k;
        off += k;
        n -= k;
    }
    return 1;
}

int __wrap__read_r(struct _reent *r, int fd, void *buf, size_t n)
{
    if (fd >= 0 && fd < MAX_FDS && fds[fd].kind != FD_NONE)
    {
        lock();
        FdState *f = &fds[fd];
        if (f->kind == FD_SERVED)
        {
            int64_t left = (int64_t)f->m->size - f->pos;
            size_t k = left <= 0 ? 0 : (left < (int64_t)n ? (size_t)left : n);
            int ok = !k || copyFromPack(f->m->offset + (uint32_t)f->pos, buf, (uint32_t)k);
            if (ok)
                f->pos += k;
            unlock();
            if (!ok)
            {
                r->_errno = EIO;
                return -1;
            }
            return (int)k;
        }
        unlock();
    }
    int res = __real__read_r(r, fd, buf, n);
    if (res > 0 && fd >= 0 && fd < MAX_FDS && fds[fd].kind == FD_RECORD)
    {
        lock();
        FdState *f = &fds[fd];
        if (f->kind == FD_RECORD)
        {
            if (f->rpos == f->len && f->len + (uint32_t)res <= f->size)
            {
                if (!f->buf)
                    f->buf = malloc(f->size);
                if (f->buf)
                {
                    memcpy(f->buf + f->len, buf, res);
                    f->len += res;
                }
            }
            f->rpos += res;
            if (f->buf && f->len == f->size && mode == 1)
                commit(f);
        }
        unlock();
    }
    return res;
}

_off_t __wrap__lseek_r(struct _reent *r, int fd, _off_t off, int whence)
{
    if (fd >= 0 && fd < MAX_FDS && fds[fd].kind == FD_SERVED)
    {
        lock();
        FdState *f = &fds[fd];
        if (f->kind == FD_SERVED)
        {
            int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos : (int64_t)f->m->size;
            int64_t np = base + off;
            if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
                np = -1;
            if (np < 0)
            {
                unlock();
                r->_errno = EINVAL;
                return -1;
            }
            f->pos = np;
            unlock();
            return (_off_t)np;
        }
        unlock();
    }
    _off_t res = __real__lseek_r(r, fd, off, whence);
    if (res >= 0 && fd >= 0 && fd < MAX_FDS && fds[fd].kind == FD_RECORD)
    {
        lock();
        if (fds[fd].kind == FD_RECORD)
            fds[fd].rpos = res;
        unlock();
    }
    return res;
}

int __wrap__fstat_r(struct _reent *r, int fd, struct stat *st)
{
    if (fd >= 0 && fd < MAX_FDS && fds[fd].kind == FD_SERVED)
    {
        lock();
        FdState *f = &fds[fd];
        if (f->kind == FD_SERVED)
        {
            memset(st, 0, sizeof(*st));
            st->st_mode = S_IFREG;
            st->st_size = f->m->size;
            st->st_atime = f->m->atime;
            st->st_mtime = f->m->mtime;
            st->st_ctime = f->m->ctime;
            unlock();
            r->_errno = 0;
            return 0;
        }
        unlock();
    }
    return __real__fstat_r(r, fd, st);
}

/* Called by the _close_r wrapper after the real close. */
void vitaBootPackClosed(int fd)
{
    if (fd < 0 || fd >= MAX_FDS || fds[fd].kind == FD_NONE)
        return;
    lock();
    FdState *f = &fds[fd];
    if (f->kind == FD_RECORD)
    {
        free(f->path);
        free(f->buf);
    }
    memset(f, 0, sizeof(*f));
    unlock();
}

/* Once per frame: close the recording when the game has stopped reading new
 * files; drop a pack that no longer matches the game. */
void vitaBootPackTick(void)
{
    if (!mode || reported)
        return;
    uint64_t now = sceKernelGetProcessTimeWide();
    if (now - lastActivity < IDLE_US)
        return;
    lock();
    if (mode == 1)
        stopRecording(1);
    else if (mode == 2)
    {
        fprintf(stderr, "Boot pack: served %u, changed %u, not in pack %u\n", (unsigned)served,
                (unsigned)stale, (unsigned)missed);
        /* Many changed files (a game update): record afresh next start. A
         * few (files the game rewrites itself) are just read from the card. */
        if (stale * 20 > served + stale)
            sceIoRemove(packPath);
        reported = 1;
    }
    unlock();
}

int vitaBootPackStats(char *buf, int size)
{
    return snprintf(buf, size, "mode=%d members=%u bytes=%u served=%u stale=%u missed=%u", mode,
                    (unsigned)memberCount, (unsigned)packBytes, (unsigned)served, (unsigned)stale,
                    (unsigned)missed);
}
