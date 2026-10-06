#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <reent.h>
#include <stdio.h>
#include <unistd.h>

static const char logPath[] = "ux0:/data/hardrpg/runtime.log";

void vitaRuntimeLogInit(void)
{
    sceIoMkdir("ux0:/data/hardrpg", 0777);
    SceIoStat info = {0};
    if (sceIoGetstat(logPath, &info) >= 0 && info.st_size > 262144) {
        sceIoRemove("ux0:/data/hardrpg/runtime.log.prev");
        sceIoRename(logPath, "ux0:/data/hardrpg/runtime.log.prev");
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

extern _ssize_t __real__write_r(struct _reent *, int, const void *, size_t);

_ssize_t __wrap__write_r(struct _reent *r, int fd, const void *data, size_t size)
{
    /* Leave game files and redirected streams on the ordinary write path.
     * Only the default console sinks are best-effort. Do not retain their
     * storage handle over standby or turn a failed log write into a game error. */
    if ((fd != 1 && fd != 2) || !isatty(fd))
        return __real__write_r(r, fd, data, size);

    int handle = sceIoOpen(logPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (handle >= 0) {
        const char *cursor = data;
        size_t remaining = size;
        while (remaining) {
            int written = sceIoWrite(handle, cursor, remaining);
            if (written <= 0) break;
            cursor += written;
            remaining -= written;
        }
        sceIoClose(handle);
    }
    r->_errno = 0;
    return size;
}
