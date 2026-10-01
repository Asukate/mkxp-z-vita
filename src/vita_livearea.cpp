#include "vita_livearea.h"

#ifdef __vita__

#include <vitasdk.h>

#include <cstring>

bool vitaHandleLiveAreaLaunch()
{
    SceAppUtilInitParam initParam{};
    SceAppUtilBootParam bootParam{};
    if (sceAppUtilInit(&initParam, &bootParam) < 0)
        return false;

    SceAppUtilAppEventParam eventParam{};
    if (sceAppUtilReceiveAppEvent(&eventParam) < 0)
        return false;

    /* 0x05 is the LiveArea launch event used by GTA:SA Vita's companion
     * launcher.  Normal Start launches do not contain our -config target. */
    if (eventParam.type != 0x05)
        return false;

    char target[2048]{};
    if (sceAppUtilAppEventParseLiveArea(&eventParam, target) < 0)
        return false;

    if (!std::strstr(target, "-config"))
        return false;

    /* Keep the configurator a separate SELF, like GTA:SA Vita. */
    sceAppMgrLoadExec("app0:/configurator.bin", nullptr, nullptr);

    /* Successful LoadExec replaces this process.  If it returns, do not
     * accidentally boot the game after the user explicitly chose Settings. */
    return true;
}

#endif
