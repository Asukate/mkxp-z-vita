#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace PerformanceMetrics {
struct Reading {
    double fps = 0, frameMs = 0, peakMs = 0;
    bool ready = false;
};

// Presentation intervals include script work, rendering and frame pacing.
// Fixed time buckets retain stalls without allocating per-frame records.
class Sampler {
    struct Bucket { uint64_t start = 0, peak = 0; bool used = false; };
    std::array<Bucket, 51> buckets{};
    uint64_t previous = 0, elapsed = 0;
    unsigned frames = 0;
    bool started = false;
    uint64_t sequence = 0;
    Reading current;
public:
    void reset() {
        buckets = {};
        previous = elapsed = frames = 0;
        started = false;
        current = {};
        ++sequence;
    }
    void present(uint64_t microseconds) {
        if (!started || microseconds <= previous) {
            reset();
            started = true;
            previous = microseconds;
            return;
        }
        const uint64_t interval = microseconds - previous;
        previous = microseconds;
        const uint64_t start = microseconds / 100000 * 100000;
        auto &bucket = buckets[(start / 100000) % buckets.size()];
        if (!bucket.used || bucket.start != start)
            bucket = {start, 0, true};
        bucket.peak = std::max(bucket.peak, interval);
        elapsed += interval;
        ++frames;
        if (elapsed >= 500000) {
            current.fps = double(frames) * 1000000 / elapsed;
            current.frameMs = double(elapsed) / frames / 1000;
            current.ready = true;
            elapsed = frames = 0;
            uint64_t peak = 0;
            for (const auto &b : buckets)
                if (b.used && microseconds >= b.start &&
                    microseconds - b.start <= 5000000)
                    peak = std::max(peak, b.peak);
            current.peakMs = double(peak) / 1000;
            ++sequence;
        }
    }
    const Reading &reading() const { return current; }
    uint64_t serial() const { return sequence; }
};

struct Placement { int x, y, width, height; bool border; };
inline std::string compactValue(double value) {
    if (value >= 9999.5) return ">9K";
    char text[16];
    std::snprintf(text, sizeof(text), value < 99.95 ? "%.1f" : "%.0f", value);
    return text;
}
inline std::string panelText(const Reading &reading, bool detailed) {
    std::string text = "FPS " + (reading.ready ? compactValue(reading.fps) : "-");
    if (detailed)
        text += "\nMS " + (reading.ready ? compactValue(reading.frameMs) : "-") +
                "\nPEAK " + (reading.ready ? compactValue(reading.peakMs) : "-");
    return text;
}
// The game FBO is flipped by the final blit; direct screen quads need this
// conversion themselves to keep top-down UI coordinates upright.
inline int framebufferY(int height, int screenY) { return height - screenY; }
inline Placement place(int width, int height, int gameX, int gameY,
                       int gameWidth, int gameHeight, bool detailed) {
    const int panelWidth = 118, panelHeight = detailed ? 62 : 26, gap = 6;
    Placement p{gap, std::max(0, height - panelHeight - gap),
                std::min(panelWidth, width), std::min(panelHeight, height), false};
    if (gameX >= panelWidth + gap) p.border = true;
    else if (width - gameX - gameWidth >= panelWidth + gap) {
        p.x = std::max(0, width - panelWidth - gap);
        p.border = true;
    } else if (height - gameY - gameHeight >= panelHeight + gap)
        p.border = true;
    else if (gameY >= panelHeight + gap) { p.y = gap; p.border = true; }
    p.x = std::max(0, std::min(p.x, width - p.width));
    p.y = std::max(0, std::min(p.y, height - p.height));
    return p;
}
} // namespace PerformanceMetrics
