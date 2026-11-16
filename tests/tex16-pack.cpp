// Host test for src/display/tex16.h (straight-alpha dithered 16-bit packing).
// Build: g++ -O2 -std=c++17 -Isrc/display tests/tex16-pack.cpp -o /tmp/t && /tmp/t
#include "tex16.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    using namespace tex16;
    // quant() is the exact integer form of floor(v*L/255 + (t+.5)/16).
    for (unsigned L : {15u, 31u, 63u})
        for (unsigned v = 0; v < 256; ++v)
            for (unsigned t = 0; t < 16; ++t) {
                const unsigned ref = (unsigned)std::floor(v * (double)L / 255.0 + (t + 0.5) / 16.0);
                CHECK(quant(v, L, t) == ref, "quant v=%u L=%u t=%u", v, L, t);
            }
    // Classification.
    std::vector<uint8_t> px(64 * 64 * 4);
    for (size_t i = 0; i < px.size(); ++i) px[i] = (uint8_t)(i * 37);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    CHECK(classify(px.data(), 64 * 64) == k565, "opaque -> 565");
    px[3 + 4 * 100] = 0;
    CHECK(classify(px.data(), 64 * 64) == k5551, "0/255 -> 5551");
    px[3 + 4 * 200] = 128;
    CHECK(classify(px.data(), 64 * 64) == k4444, "soft -> 4444");
    // Straight alpha: 5551 keeps colour under alpha 0; 4444 has no rgb<=a clamp.
    const uint8_t clear[4] = {200, 100, 50, 0};
    const uint16_t v5 = packPixel(clear, 7, k5551);
    CHECK((v5 & 1) == 0 && (v5 >> 1) != 0, "5551 transparent keeps rgb (got %04x)", v5);
    const uint8_t faint[4] = {255, 255, 255, 17};
    uint8_t back[4];
    unpack(packPixel(faint, 7, k4444), k4444, back);
    CHECK(back[0] == 255 && back[1] == 255 && back[2] == 255, "4444 faint white stays white");
    // Average of a dithered flat area is close to the 8-bit value (dither works).
    for (int fmt = k565; fmt <= k4444; ++fmt)
        for (int v = 0; v < 256; v += 5) {
            std::vector<uint8_t> flat(16 * 16 * 4), out(16 * 16 * 2);
            for (int i = 0; i < 256; ++i) { flat[4*i] = flat[4*i+1] = flat[4*i+2] = (uint8_t)v; flat[4*i+3] = 255; }
            pack(flat.data(), out.data(), 16, 16, 0, (uint8_t)fmt);
            double sum = 0;
            for (int i = 0; i < 256; ++i) { unpack((uint16_t)(out[2*i] | out[2*i+1] << 8), (uint8_t)fmt, back); sum += back[0]; }
            CHECK(std::fabs(sum / 256 - v) <= 1.5, "fmt %d flat %d avg %.2f", fmt, v, sum / 256);
        }
    // In-place packing equals out-of-place packing.
    std::vector<uint8_t> a(37 * 11 * 4), b, o(37 * 11 * 2);
    for (size_t i = 0; i < a.size(); ++i) a[i] = (uint8_t)(i * 91 + 7);
    b = a;
    pack(a.data(), o.data(), 37, 11, 0, k4444);
    pack(b.data(), b.data(), 37, 11, 0, k4444);
    CHECK(std::equal(o.begin(), o.end(), b.begin()), "in-place pack");
    std::printf(fails ? "tex16-pack: %d FAILED\n" : "tex16-pack: OK\n", fails);
    return fails ? 1 : 0;
}
