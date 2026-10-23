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

void VitaStats::build(int level, const PerformanceMetrics::Reading &frames, std::string &headline, int &tone,
                      std::vector<hardrpg::View::StatRow> &rows) {
    using hardrpg::tr;
    rows.clear();
    if (frames.ready) {
        headline = format("%.0f FPS", frames.fps);
        tone = frames.fps >= 55 ? 1 : frames.fps >= 30 ? 2 : 3;
    } else {
        headline = "- FPS";
        tone = 0;
    }
    if (level < 2)
        return;
    if (frames.ready)
        rows.push_back({tr("Frame"), format("%.1f ms", frames.frameMs) + "  \xc2\xb7  " + tr("peak") + " " +
                                         format("%.0f ms", frames.peakMs),
                        frames.peakMs > 50 ? 3 : frames.peakMs > 25 ? 2 : 0});
    if (level < 3)
        return;
    sample();
    if (cpu >= 0)
        rows.push_back({tr("CPU"), format("%.0f%%", cpu) + " " + tr("main thread") + "  \xc2\xb7  " +
                                       format("%.0f MHz", armMHz),
                        cpu >= 95 ? 3 : cpu >= 80 ? 2 : 0});
    rows.push_back({tr("GPU"), format("%.0f MHz", gpuMHz) + "  \xc2\xb7  " + tr("video") + " " +
                                   format("%.0f/%.0f MB", vramUsed, vramTotal),
                    vramTotal > 0 && vramUsed > vramTotal * 0.9 ? 2 : 0});
    rows.push_back({tr("Memory"), format("%.0f/%.0f MB", heapUsed, heapTotal) + "  \xc2\xb7  " + tr("free") + " " +
                                      format("%.0f MB", freeRam),
                    heapTotal > 0 && heapUsed > heapTotal * 0.9 ? 3 : heapUsed > heapTotal * 0.8 ? 2 : 0});
    if (battery >= 0) {
        std::string value = format("%.0f%%", battery);
        if (charging)
            value += "  \xc2\xb7  " + std::string(tr("charging"));
        else if (lifeMinutes > 0)
            value += "  \xc2\xb7  " + format("%.0f h %02.0f min", lifeMinutes / 60, lifeMinutes % 60);
        if (tempTenths > 0)
            value += "  \xc2\xb7  " + format("%.1f \xc2\xb0" "C", tempTenths / 10.0);
        rows.push_back({tr("Battery"), value, battery <= 10 ? 3 : battery <= 20 ? 2 : 0});
        rows.push_back({tr("Power draw"),
                        charging ? std::string(tr("on the charger"))
                        : milliwatts > 0 ? format("\xe2\x89\x88 %.2f W", milliwatts / 1000.0)
                                         : std::string(tr("measuring...")),
                        0});
    }
}
