/*
 * ghc_vita_stubs.h — POSIX function stubs missing from Vita's newlib
 *
 * Some POSIX functions that GHC::filesystem (and mkxp-z) expect
 * are not available on PS Vita's newlib C library. This header
 * provides inline stubs for the missing ones.
 *
 * NOTE: strcasecmp, strncasecmp exist in Vita's <strings.h>.
 * strerror_r exists as GNU version (char* return) on Vita.
 * We include string.h BEFORE defining our strerror_r to avoid
 * double-declaration issues.
 */

#ifndef GHC_VITA_STUBS_H
#define GHC_VITA_STUBS_H

/* Force _GNU_SOURCE so string.h declares the GNU strerror_r (char* return) */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <errno.h>
#include <string.h>
#include <strings.h>   /* strcasecmp, strncasecmp */
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>

/*
 * strerror_r — ghc/filesystem.hpp calls strerror_r for error messages.
 * The Vita SDK's <string.h> conditionally declares either:
 *   - GNU version: char *strerror_r(int, char*, size_t)  (with _GNU_SOURCE)
 *   - POSIX version: int strerror_r(int, char*, size_t)  (without _GNU_SOURCE)
 * ghc handles both via strerror_adapter() overloads.
 *
 * We DO NOT provide our own strerror_r — the system one always exists
 * on Vita SDK. If it's somehow missing, a compile error will tell us.
 */

/* strnlen — missing from Vita's newlib */
#if !defined(strnlen)
inline size_t strnlen(const char *s, size_t maxlen)
{
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* symlink — not supported on Vita, return ENOSYS */
inline int symlink(const char *target, const char *linkpath)
{
    (void)target;
    (void)linkpath;
    errno = ENOSYS;
    return -1;
}

/* readlink — not supported on Vita, return ENOSYS */
inline ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    (void)path;
    (void)buf;
    (void)bufsiz;
    errno = ENOSYS;
    return -1;
}

/* AT_FDCWD and AT_SYMLINK_NOFOLLOW for utimensat */
#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#ifndef UTIME_OMIT
#define UTIME_OMIT ((1l << 30) - 2l)
#endif

/* utimensat — missing from Vita newlib */
inline int utimensat(int dirfd, const char *pathname,
                     const struct timespec times[2], int flags)
{
    (void)dirfd;
    (void)pathname;
    (void)times;
    (void)flags;
    /* Try fallback via utimes */
    if (times) {
        struct timeval tv[2];
        tv[0].tv_sec = times[0].tv_sec;
        tv[0].tv_usec = times[0].tv_nsec / 1000;
        tv[1].tv_sec = times[1].tv_sec;
        tv[1].tv_usec = times[1].tv_nsec / 1000;
        return utimes(pathname, tv);
    }
    return utimes(pathname, NULL);
}

/* truncate — missing from Vita newlib */
inline int truncate(const char *path, off_t length)
{
    (void)path;
    (void)length;
    errno = ENOSYS;
    return -1;
}

#ifdef __cplusplus
}
#endif

#endif /* GHC_VITA_STUBS_H */
