/* Counts the times this process may have added a file or folder: an open
 * for writing, a rename, a mkdir. Ruby, SDL and PhysFS all reach these
 * through newlib, linked with --wrap. FileSystem uses the count to know
 * when a directory listing it holds can still be trusted. */
#include <fcntl.h>
#include <reent.h>
#include <stdatomic.h>
#include <strings.h>

static atomic_uint writeGeneration;

unsigned vitaFileWriteGeneration(void)
{
    return atomic_load_explicit(&writeGeneration, memory_order_acquire);
}

/* HardRPG's own files (logs, reports, crash copies, the image-load history)
 * are written all the time and never hold game assets; games and RTPs under
 * ux0:/data/hardrpg/games and /rtp still count, as does anything elsewhere. */
static int engineOwnPath(const char *path)
{
    static const char *const roots[] = {"ux0:/data/hardrpg/", "ux0:data/hardrpg/"};
    for (unsigned i = 0; path && i < sizeof(roots) / sizeof(roots[0]); ++i)
    {
        const size_t n = __builtin_strlen(roots[i]);
        if (strncasecmp(path, roots[i], n) != 0)
            continue;
        const char *rest = path + n;
        return strncasecmp(rest, "games/", 6) != 0 && strncasecmp(rest, "rtp/", 4) != 0 &&
               strcasecmp(rest, "games") != 0 && strcasecmp(rest, "rtp") != 0;
    }
    return 0;
}

static void noteWrite(const char *path)
{
    if (!engineOwnPath(path))
        atomic_fetch_add_explicit(&writeGeneration, 1, memory_order_acq_rel);
}

extern int __real__open_r(struct _reent *, const char *, int, int);
extern int __real__rename_r(struct _reent *, const char *, const char *);
extern int __real__mkdir_r(struct _reent *, const char *, int);

int __wrap__open_r(struct _reent *r, const char *path, int flags, int mode)
{
    const int result = __real__open_r(r, path, flags, mode);
    /* Counted after the call, so a listing made after the count was read
     * already shows the file. */
    if ((flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) != 0)
        noteWrite(path);
    return result;
}

int __wrap__rename_r(struct _reent *r, const char *from, const char *to)
{
    const int result = __real__rename_r(r, from, to);
    noteWrite(to);
    return result;
}

int __wrap__mkdir_r(struct _reent *r, const char *path, int mode)
{
    const int result = __real__mkdir_r(r, path, mode);
    noteWrite(path);
    return result;
}
