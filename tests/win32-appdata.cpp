// Run inside an empty scratch directory: "ux0:" is a host test mount.
#include "util/win32-appdata.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>

int main() {
    assert(mkdir("ux0:", 0700) == 0);
    unsetenv("AV_APPDATA");
    assert(hardrpg::prepareWindowsAppData());
    assert(std::strcmp(getenv("AV_APPDATA"), "ux0:/data/hardrpg/saves") == 0);
    const char *game = "ux0:/data/hardrpg/saves/example-game";
    assert(mkdir(game, 0700) == 0);
    FILE *f = fopen("ux0:/data/hardrpg/saves/example-game/music.txt", "wb");
    assert(f && fwrite("true", 1, 4, f) == 4 && fclose(f) == 0);
    assert(hardrpg::prepareWindowsAppData()); // existing settings stay intact
    f = fopen("ux0:/data/hardrpg/saves/example-game/music.txt", "rb");
    char contents[5] = {};
    assert(f && fread(contents, 1, 4, f) == 4 && fclose(f) == 0);
    assert(std::strcmp(contents, "true") == 0);

    // Existing folders are used as they are, even in a parent that refuses
    // new entries (a launcher-started game cannot add to ux0:/data).
    assert(chmod("ux0:", 0500) == 0);
    unsetenv("AV_APPDATA");
    assert(hardrpg::prepareWindowsAppData());
    assert(std::strcmp(getenv("AV_APPDATA"), "ux0:/data/hardrpg/saves") == 0);
    assert(chmod("ux0:", 0700) == 0);

    // An existing regular file must never be reported as a usable directory.
    assert(unlink("ux0:/data/hardrpg/saves/example-game/music.txt") == 0);
    assert(rmdir(game) == 0 && rmdir("ux0:/data/hardrpg/saves") == 0);
    f = fopen("ux0:/data/hardrpg/saves", "wb");
    assert(f && fclose(f) == 0);
    setenv("AV_APPDATA", "unchanged-on-failure", 1);
    const char *failed = nullptr;
    assert(!hardrpg::prepareWindowsAppData(&failed));
    assert(failed && std::strcmp(failed, "ux0:/data/hardrpg/saves") == 0 && errno == ENOTDIR);
    assert(std::strcmp(getenv("AV_APPDATA"), "unchanged-on-failure") == 0);
    puts("Win32 application-data preparation passed");
}
