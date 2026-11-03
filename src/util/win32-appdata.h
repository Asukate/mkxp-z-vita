#ifndef HARDRPG_WIN32_APPDATA_H
#define HARDRPG_WIN32_APPDATA_H

#include <cerrno>
#include <cstdlib>
#include <sys/stat.h>

namespace hardrpg {

// The Windows Utils!AVSetEnv contract: create a writable application-data
// parent and publish it for games to append their own subdirectory.
inline bool prepareWindowsAppData() {
    const char *paths[] = {"ux0:/data", "ux0:/data/hardrpg", "ux0:/data/hardrpg/saves"};
    for (const char *path : paths) {
        struct stat st;
        if (mkdir(path, 0777) != 0 &&
            (errno != EEXIST || stat(path, &st) != 0 || !S_ISDIR(st.st_mode)))
            return false;
    }
    return setenv("AV_APPDATA", paths[2], 1) == 0;
}

}
#endif
