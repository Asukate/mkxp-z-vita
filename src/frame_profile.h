#ifndef MKXP_FRAME_PROFILE_H
#define MKXP_FRAME_PROFILE_H
#include <stdint.h>
#include <string.h>
// Single RGSS/render thread only. No allocations, logging, or GPU fences.
namespace FrameProfile {
enum Category {
    Unattributed, TextPrep, TextMeasure, TextRaster, TextFormat,
    TextOutline, TextTemporary, TextBlit, TextCleanup, BlitPrepare,
    DestinationCopy, BlitSubmit, TextureUpload, TextureAllocate,
    GraphicsOther, SceneComposite, PrepareDraw, ScreenBlit,
    Present, Pacing, FormatConvert, Shadow, OutlineBlend, TtfRaster,
    TtfMeasure, Profiler, Diagnostics, HeapCheckpoint, GarbageCollection, DrawSprite, DrawWindow, DrawTilemap, DrawPlane,
    WindowPrepare, BitmapFile, ImageReadDecode, SceneSetup, FrameClear, GeometryUpload, DrawElementsCall, SpriteShaderState, UploadCowAllocate, UploadCowCopy, UploadRetire, UploadWait, UploadCopy, UploadConvert, FramebufferBind, RenderTargetAlloc, ShaderBind, ClearSync, ClearSetup, ClearSubmit, SceneEnd, SceneBegin, ClearFull, ClearPartial, ClearNextDraw, ClearNextRead, ClearReplaced, ClearDiscarded, ShaderCompile, ShaderCache, FontOpen, NativeCount
};
constexpr unsigned Categories = 128, Capacity = 512, Depth = 64;
struct Cost { uint64_t ticks, calls, pixels; };
struct Record { uint64_t ticks; unsigned kind; Cost costs[Categories]; };
struct State {
    bool enabled, capture;
    unsigned depth, size, errors, dropped;
    unsigned stack[Depth];
    uint64_t last, start;
    Cost costs[Categories];
    Record records[Capacity];
};
#ifdef MKXPZ_VITA_DIAGNOSTICS
extern State state;
uint64_t clock();
inline void charge() {
    uint64_t now = clock();
    if (state.capture)
        state.costs[state.depth ? state.stack[state.depth-1] : 0].ticks += now-state.last;
    state.last = now;
}
inline bool enter(unsigned id, uint64_t pixels = 0) {
    if (!state.enabled) return false;
    if (id >= Categories || state.depth == Depth) { ++state.errors; return false; }
    charge(); state.stack[state.depth++] = id;
    if (state.capture) { ++state.costs[id].calls; state.costs[id].pixels += pixels; }
    return true;
}
inline void leave() {
    charge();
    if (!state.depth) { ++state.errors; return; }
    --state.depth;
}
inline void next(unsigned id) {
    if (!state.enabled) return;
    if (!state.depth || id >= Categories) { ++state.errors; return; }
    charge(); state.stack[state.depth-1] = id;
    if (state.capture) ++state.costs[id].calls;
}
// kind: 0 partial start, 1 full presentation interval, 2 partial tail.
inline void boundary(unsigned kind = 1) {
    if (!state.enabled || !state.capture) return;
    charge();
    if (state.size < Capacity) {
        Record &r = state.records[state.size++];
        r.ticks = state.last-state.start;
        r.kind = state.size == 1 && kind == 1 ? 0 : kind;
        memcpy(r.costs, state.costs, sizeof(state.costs));
    } else ++state.dropped;
    memset(state.costs, 0, sizeof(state.costs)); state.start = state.last;
}
inline void begin() {
    state.enabled = true; state.capture = true;
    state.size = state.errors = state.dropped = 0;
    memset(state.costs, 0, sizeof(state.costs));
    state.start = state.last = clock();
}
inline void end() { boundary(2); state.capture = false; }
#else
inline bool enter(unsigned, uint64_t = 0) { return false; }
inline void leave() {}
inline void next(unsigned) {}
inline void boundary(unsigned = 1) {}
inline void begin() {}
inline void end() {}
#endif
struct Scope {
    bool active;
    explicit Scope(unsigned id, uint64_t pixels = 0) : active(enter(id, pixels)) {}
    ~Scope() { if (active) leave(); }
    void stage(unsigned id) { if (active) next(id); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};
}
#endif
