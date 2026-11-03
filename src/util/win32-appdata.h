#ifndef HARDRPG_WIN32_APPDATA_H
#define HARDRPG_WIN32_APPDATA_H

#include <cerrno>
#include <cstdlib>
#include <sys/stat.h>

namespace hardrpg {

// Uses an existing directory as it is and creates only a missing one. A game
// started from the launcher (LoadExec) may not create entries in ux0:/data
// itself: mkdir there fails with EACCES even though the folder exists.
inline bool ensureDirectory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return true;
        errno = ENOTDIR;
        return false;
    }
    if (mkdir(path, 0777) == 0)
        return true;
    const int error = errno;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return true;
    errno = error;
    return false;
}

// The Windows Utils!AVSetEnv contract: create a writable application-data
// parent and publish it for games to append their own subdirectory.
// On failure errno is set and *failedPath names the folder that failed.
inline bool prepareWindowsAppData(const char **failedPath = nullptr) {
    static const char *const paths[] = {"ux0:/data", "ux0:/data/hardrpg", "ux0:/data/hardrpg/saves"};
    for (const char *path : paths)
        if (!ensureDirectory(path)) {
            if (failedPath)
                *failedPath = path;
            return false;
        }
    return setenv("AV_APPDATA", paths[2], 1) == 0;
}

}
#endif
