#include "vita-stats.h"
#include "native_launcher/i18n.h"
#include <algorithm>
#include <cstdio>
#include <malloc.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <vitaGL.h>

extern "C" unsigned int _newlib_heap_size_user;

namespace {
std::string format(const char *pattern, double a, double b = 0) {
    char text[64];
    std::snprintf(text, sizeof text, pattern, a, b);
    return text;
}
} // namespace

void VitaStats::sample() {
    const uint64_t now = sceKernelGetProcessTimeWide();
    if (sampledAt && now - sampledAt < 1000000)
        return;
    // The game's main thread: its run time against the time passed.
    SceKernelThreadInfo info = {};
    info.size = sizeof info;
    if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &info) >= 0) {
        const uint64_t run = info.runClocks;
        if (sampledAt && now > sampledAt)
            cpu = int(std::min<uint64_t>(100, (run - lastRun) * 100 / (now - sampledAt)));
        lastRun = run;
    }
    sampledAt = now;
    armMHz = scePowerGetArmClockFrequency();
    gpuMHz = scePowerGetGpuClockFrequency();
    struct mallinfo heap = mallinfo();
    heapUsed = heap.uordblks / 1048576.0;
    heapTotal = _newlib_heap_size_user / 1048576.0;
    SceKernelFreeMemorySizeInfo memory = {};
    memory.size = sizeof memory;
    if (sceKernelGetFreeMemorySize(&memory) >= 0)
        freeRam = memory.size_user / 1048576.0;
    vramTotal = vglMemTotal(VGL_MEM_VRAM) / 1048576.0;
    vramUsed = vramTotal - vglMemFree(VGL_MEM_VRAM) / 1048576.0;
    battery = scePowerGetBatteryLifePercent();
    lifeMinutes = scePowerGetBatteryLifeTime();
    int temp = scePowerGetBatteryTemp(); // hundredths of a degree
    tempTenths = temp > 0 ? (temp > 1000 ? temp / 10 : temp * 10) : -1;
    millivolts = scePowerGetBatteryVolt();
    charging = scePowerIsBatteryCharging() || scePowerIsPowerOnline();
    // Power drawn: the charge lost over the last few minutes, at the battery's
    // voltage. The charge is counted in whole mAh, so it needs a minute.
    const int mAh = scePowerGetBatteryRemainCapacity();
    if (charging || mAh <= 0) {
        charge.clear();
        milliwatts = -1;
        return;
    }
    charge.emplace_back(now, mAh);
    while (charge.size() > 2 && now - charge.front().first > 180000000ull)
        charge.pop_front();
    const double seconds = (now - charge.front().first) / 1e6;
    const int lost = charge.front().second - mAh;
    if (seconds >= 60 && lost >= 2 && millivolts > 0)
        milliwatts = int(lost / (seconds / 3600.0) * millivolts / 1000.0);
}

void VitaStats::build(int level, int rgssVersion, const PerformanceMetrics::Reading &frames, std::string &headline,
                      int &tone, std::vector<hardrpg::View::StatRow> &rows) {
    using hardrpg::tr;
    rows.clear();
    /* XP games target 40 fps (25 ms frames), VX and VX Ace 60. */
    const bool xp = rgssVersion == 1;
    if (frames.ready) {
        headline = format("%.0f FPS", frames.fps);
        tone = frames.fps >= (xp ? 37 : 55) ? 1 : frames.fps >= (xp ? 20 : 30) ? 2 : 3;
    } else {
        headline = "- FPS";
        tone = 0;
    }
    if (level < 2)
        return;
    /* Average frame time over the last half second, then the slowest frame. */
    if (frames.ready)
        rows.push_back({"", format("%.0f / %.0f ms", frames.frameMs, frames.peakMs),
                        frames.peakMs > (xp ? 75 : 50) ? 3 : frames.peakMs > (xp ? 37 : 25) ? 2 : 0});
    if (level < 3)
        return;
    sample();
    /* One narrow column: each value on its own line under a caption. */
    if (cpu >= 0)
        rows.push_back({tr("CPU"), format("%.0f%%\n%.0f MHz", cpu, armMHz), cpu >= 95 ? 3 : cpu >= 80 ? 2 : 0});
    rows.push_back({tr("GPU"), format("%.0f MHz", gpuMHz) + "\n" + format("%.0f/%.0f MB", vramUsed, vramTotal),
                    vramTotal > 0 && vramUsed > vramTotal * 0.9 ? 2 : 0});
    rows.push_back({tr("Memory"), format("%.0f/%.0f MB", heapUsed, heapTotal) + "\n" + tr("free") + " " +
                                      format("%.0f MB", freeRam),
                    heapTotal > 0 && heapUsed > heapTotal * 0.9 ? 3 : heapUsed > heapTotal * 0.8 ? 2 : 0});
    if (battery >= 0) {
        std::string value = format("%.0f%%", battery);
        if (charging)
            value += "\n" + std::string(tr("charging"));
        else if (lifeMinutes > 0)
            value += "  " + format("%.0fh%02.0f", lifeMinutes / 60, lifeMinutes % 60);
        if (tempTenths > 0)
            value += "\n" + format("%.1f \xc2\xb0" "C", tempTenths / 10.0);
        rows.push_back({tr("Battery"), value, battery <= 10 ? 3 : battery <= 20 ? 2 : 0});
        rows.push_back({tr("Power draw"),
                        charging ? std::string(tr("on the charger"))
                        : milliwatts > 0 ? format("\xe2\x89\x88 %.2f W", milliwatts / 1000.0)
                                         : std::string(tr("measuring...")),
                        0});
    }
}
