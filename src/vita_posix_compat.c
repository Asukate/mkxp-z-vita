/*
 * POSIX compatibility used by the embedded Ruby runtime.
 *
 * The Vita SDK exposes several of these declarations through newlib, but
 * does not provide the implementations. Keep this file C-only so the same
 * object can be linked by Ruby's bootstrap interpreter and by mkxp-z.
 */
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>

/* The vitasdk pthread library defaults user thread stacks to 16 KiB,
 * which overflows when SDL/mkxp-z threads run vorbis decoding or deep
 * C++ call chains. Override the weak default to 1 MiB. */
unsigned int _pthread_stack_default_user = 0x100000;
#include <unistd.h>

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)addr;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;

    void *memory = malloc(length);
    if (!memory)
        errno = ENOMEM;
    return memory ? memory : MAP_FAILED;
}

int mprotect(void *addr, size_t len, int prot)
{
    (void)addr;
    (void)len;
    (void)prot;
    /* Vita has no user-space page protection API. */
    return 0;
}

int munmap(void *addr, size_t length)
{
    (void)length;
    free(addr);
    return 0;
}

int dup2(int oldfd, int newfd)
{
    if (oldfd == newfd)
        return newfd;
    if (oldfd < 0 || newfd < 0) {
        errno = EBADF;
        return -1;
    }

    int duplicate = fcntl(oldfd, F_DUPFD, newfd);
    if (duplicate < 0)
        return -1;
    if (duplicate != newfd) {
        close(duplicate);
        errno = EMFILE;
        return -1;
    }
    return duplicate;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    (void)pid;
    (void)status;
    (void)options;
    errno = ENOSYS;
    return (pid_t)-1;
}

int getpagesize(void)
{
    return 4096;
}

long sysconf(int name)
{
#ifdef _SC_PAGESIZE
    if (name == _SC_PAGESIZE)
        return 4096;
#endif
#ifdef _SC_PAGE_SIZE
    if (name == _SC_PAGE_SIZE)
        return 4096;
#endif

    errno = EINVAL;
    return -1;
}

mode_t umask(mode_t mask)
{
    (void)mask;
    return 0;
}

pid_t getppid(void)
{
    return (pid_t)1;
}

pid_t getpgrp(void)
{
    return (pid_t)0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how;
    (void)set;
    (void)oldset;
    errno = ENOSYS;
    return -1;
}

static struct passwd vita_pwd;
static char vita_home[] = "ux0:/data";

char *getlogin(void)
{
    return (char *)"vita";
}

static struct passwd *vita_password_entry(void)
{
    vita_pwd.pw_name = (char *)"vita";
    vita_pwd.pw_dir = vita_home;
    vita_pwd.pw_uid = 0;
    vita_pwd.pw_gid = 0;
    vita_pwd.pw_shell = (char *)"";
    return &vita_pwd;
}

struct passwd *getpwuid(uid_t uid)
{
    (void)uid;
    return vita_password_entry();
}

struct passwd *getpwnam(const char *name)
{
    (void)name;
    return vita_password_entry();
}

void endpwent(void)
{
}

int execl(const char *path, const char *arg, ...)
{
    (void)path;
    (void)arg;
    errno = ENOSYS;
    return -1;
}

int execle(const char *path, const char *arg, ...)
{
    (void)path;
    (void)arg;
    errno = ENOSYS;
    return -1;
}

int execv(const char *path, char *const argv[])
{
    (void)path;
    (void)argv;
    errno = ENOSYS;
    return -1;
}
