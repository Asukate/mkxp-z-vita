#pragma once
#include "performance-metrics.h"
#include "native_launcher/view.h"
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

// What the in-game performance overlay shows beyond the frame rate, read
// from the Vita about once a second: how busy the game's main thread is,
// the CPU and GPU clocks, the game's memory heap and the free main and video
// memory, and the battery (charge, time left, temperature, and the power
// drawn, estimated from how fast the charge falls).
class VitaStats {
public:
    // level: 1 FPS, 2 FPS and frame timing, 3 everything.
    void build(int level, const PerformanceMetrics::Reading &frames, std::string &headline, int &tone,
               std::vector<hardrpg::View::StatRow> &rows);

private:
    void sample();
    uint64_t sampledAt = 0, lastRun = 0;
    int cpu = -1, armMHz = 0, gpuMHz = 0;
    double heapUsed = 0, heapTotal = 0, freeRam = 0, vramUsed = 0, vramTotal = 0;
    int battery = -1, lifeMinutes = -1, tempTenths = -1, millivolts = 0;
    bool charging = false;
    int milliwatts = -1;
    std::deque<std::pair<uint64_t, int>> charge; // (time, mAh), the last few minutes
};
