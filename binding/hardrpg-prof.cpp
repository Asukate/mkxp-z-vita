/*
** hardrpg-prof.cpp
**
** Sampling profiler for game scripts (HardRPGProf). A timer thread asks Ruby for a sample
** every interval; Ruby takes it on the game thread at its next safe point (a workqueue job),
** and the sample records the Ruby and engine (C) methods on the stack. Samples asked for
** while the game is inside Graphics.update / wait / transition / fadein / fadeout count as
** frame time (drawing and waiting for the frame), not script time.
**
**   HardRPGProf.start(interval_us = 1000)
**   HardRPGProf.stop
**   HardRPGProf.reset
**   HardRPGProf.dump(path, label, top = 80)   # appends a text report
**   HardRPGProf.stats                         # {samples:, frame:, lost:, seconds:}
**
** Off unless a script calls start; nothing runs before that.
*/

#include <ruby.h>
#include <ruby/debug.h>
#include <SDL_thread.h>
#include <SDL_timer.h>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#endif

/* Ruby's queue for jobs from non-Ruby threads (vm_trace.c). rb_postponed_job_register_one
 * reads the calling thread's Ruby context, which on the Vita build aborts on a foreign thread. */
extern "C" int rb_workqueue_register(unsigned flags, rb_postponed_job_func_t func, void *data);

/* Set by the Graphics bindings while a frame call runs (see graphics-binding.cpp). */
std::atomic<int> g_profInFrame{0};

namespace {

struct Count {
    uint32_t incl = 0;   /* samples with this method anywhere on the stack */
    uint32_t self = 0;   /* samples with this method on top */
    uint32_t selfRb = 0; /* samples with this method as the top Ruby (script) method */
};

std::atomic<bool> g_running{false};
std::atomic<bool> g_threadUp{false};
std::atomic<int> g_intervalUs{1000};
std::atomic<uint32_t> g_frameSamples{0}, g_lost{0}, g_asked{0};
std::atomic<bool> g_pending{false}; /* a sample job is queued and not yet run */
std::atomic<bool> g_shutdown{false}; /* Ruby is ending: never touch it again */
std::atomic<bool> g_inRegister{false}; /* the sampler is inside rb_workqueue_register */
uint32_t g_samples = 0;
uint64_t g_startTicks = 0;
std::unordered_map<VALUE, Count> g_counts;
VALUE g_keep = Qnil; /* frames seen, kept alive for the report */
std::unordered_set<VALUE> g_known;

void takeSample(void *) {
    g_pending = false;
    if (!g_running.load(std::memory_order_relaxed))
        return;
    VALUE frames[96];
    int lines[96];
    int n = rb_profile_frames(0, 96, frames, lines);
    if (n <= 0)
        return;
    g_samples++;
    VALUE topRb = Qnil;
    for (int i = 0; i < n; ++i) {
        VALUE f = frames[i];
        if (!g_known.count(f)) {
            g_known.insert(f);
            rb_ary_push(g_keep, f);
        }
        if (NIL_P(topRb) && !NIL_P(rb_profile_frame_path(f)))
            topRb = f;
    }
    /* inclusive: once per sample per method, even when it recurses */
    for (int i = 0; i < n; ++i) {
        bool seen = false;
        for (int j = 0; j < i; ++j)
            if (frames[j] == frames[i]) {
                seen = true;
                break;
            }
        if (!seen)
            g_counts[frames[i]].incl++;
    }
    g_counts[frames[0]].self++;
    if (!NIL_P(topRb))
        g_counts[topRb].selfRb++;
}

int samplerThread(void *) {
    while (true) {
        int us = g_intervalUs.load(std::memory_order_relaxed);
#ifdef __vita__
        sceKernelDelayThread(us);
#else
        SDL_Delay(std::max(1, us / 1000));
#endif
        if (g_shutdown.load())
            return 0;
        if (!g_running.load(std::memory_order_relaxed))
            continue;
        g_asked++;
        if (g_profInFrame.load(std::memory_order_relaxed) > 0) {
            g_frameSamples++;
            continue;
        }
        /* One job at a time: while the game is inside a long engine call the job waits, and
         * the requests made meanwhile are counted as lost rather than queued. */
        if (g_pending.exchange(true)) {
            g_lost++;
            continue;
        }
        g_inRegister = true;
        bool ok = !g_shutdown.load() && rb_workqueue_register(0, takeSample, nullptr);
        g_inRegister = false;
        if (!ok) {
            g_pending = false;
            g_lost++;
        }
    }
    return 0;
}

VALUE profStart(int argc, VALUE *argv, VALUE) {
    if (argc > 0 && !NIL_P(argv[0]))
        g_intervalUs = std::max(200, NUM2INT(argv[0]));
    if (!g_threadUp.exchange(true)) {
        SDL_Thread *t = SDL_CreateThreadWithStackSize(samplerThread, "hrpg-prof", 32 * 1024, nullptr);
        if (t)
            SDL_DetachThread(t);
    }
    if (!g_running.exchange(true))
        g_startTicks = SDL_GetTicks();
    return Qtrue;
}

VALUE profStop(VALUE) {
    g_running = false;
    return Qnil;
}

VALUE profReset(VALUE) {
    g_counts.clear();
    g_samples = 0;
    g_frameSamples = 0;
    g_lost = 0;
    g_asked = 0;
    g_startTicks = SDL_GetTicks();
    return Qnil;
}

double seconds() { return (SDL_GetTicks() - g_startTicks) / 1000.0; }

VALUE profStats(VALUE) {
    VALUE h = rb_hash_new();
    rb_hash_aset(h, ID2SYM(rb_intern("samples")), UINT2NUM(g_samples));
    rb_hash_aset(h, ID2SYM(rb_intern("frame")), UINT2NUM(g_frameSamples.load()));
    rb_hash_aset(h, ID2SYM(rb_intern("lost")), UINT2NUM(g_lost.load()));
    rb_hash_aset(h, ID2SYM(rb_intern("asked")), UINT2NUM(g_asked.load()));
    rb_hash_aset(h, ID2SYM(rb_intern("seconds")), DBL2NUM(seconds()));
    return h;
}

std::string frameName(VALUE f) {
    VALUE label = rb_profile_frame_full_label(f);
    std::string s = NIL_P(label) ? std::string("?") : std::string(RSTRING_PTR(label), RSTRING_LEN(label));
    VALUE path = rb_profile_frame_path(f);
    if (NIL_P(path)) {
        s += "\t(C)";
    } else {
        s += "\t";
        s.append(RSTRING_PTR(path), RSTRING_LEN(path));
        VALUE line = rb_profile_frame_first_lineno(f);
        if (!NIL_P(line))
            s += ":" + std::to_string(NUM2INT(line));
    }
    return s;
}

/* HardRPGProf.dump(path, label, top = 80): appends one report section. */
VALUE profDump(int argc, VALUE *argv, VALUE) {
    if (argc < 2)
        rb_raise(rb_eArgError, "dump(path, label, top = 80)");
    std::string path = StringValueCStr(argv[0]);
    std::string label = StringValueCStr(argv[1]);
    size_t top = argc > 2 ? (size_t)NUM2INT(argv[2]) : 80;
    std::vector<std::pair<VALUE, Count>> v(g_counts.begin(), g_counts.end());
    FILE *fp = fopen(path.c_str(), "a");
    if (!fp)
        return Qfalse;
    uint32_t frame = g_frameSamples.load();
    uint32_t total = g_samples + frame;
    fprintf(fp, "== %s\nseconds=%.1f interval_us=%d script_samples=%u frame_samples=%u lost=%u asked=%u\n",
            label.c_str(), seconds(), g_intervalUs.load(), g_samples, frame, g_lost.load(), g_asked.load());
    fprintf(fp, "script share=%.1f%%\n", total ? 100.0 * g_samples / total : 0.0);
    auto section = [&](const char *title, uint32_t Count::*field) {
        std::sort(v.begin(), v.end(), [&](const auto &a, const auto &b) { return a.second.*field > b.second.*field; });
        fprintf(fp, "-- %s (%% of script samples)\n", title);
        for (size_t i = 0; i < v.size() && i < top; ++i) {
            uint32_t c = v[i].second.*field;
            if (!c)
                break;
            fprintf(fp, "%6.2f%%\t%u\t%s\n", g_samples ? 100.0 * c / g_samples : 0.0, c,
                    frameName(v[i].first).c_str());
        }
    };
    section("inclusive", &Count::incl);
    section("self (top of stack, C included)", &Count::self);
    section("self Ruby (top script method)", &Count::selfRb);
    fclose(fp);
    return Qtrue;
}

/* At exit (and on reset of the interpreter): stop asking Ruby for samples before it goes away.
 * Without this the sampler kept queuing jobs while Ruby shut down, which crashed on exit. */
void profAtExit(VALUE) {
    g_running = false;
    g_shutdown = true;
    while (g_inRegister.load())
        SDL_Delay(1);
}

} // namespace

void hardrpgProfInit() {
    VALUE m = rb_define_module("HardRPGProf");
    g_keep = rb_ary_new();
    rb_gc_register_address(&g_keep);
    rb_define_module_function(m, "start", RUBY_METHOD_FUNC(profStart), -1);
    rb_define_module_function(m, "stop", RUBY_METHOD_FUNC(profStop), 0);
    rb_define_module_function(m, "reset", RUBY_METHOD_FUNC(profReset), 0);
    rb_define_module_function(m, "dump", RUBY_METHOD_FUNC(profDump), -1);
    rb_define_module_function(m, "stats", RUBY_METHOD_FUNC(profStats), 0);
    rb_set_end_proc(profAtExit, Qnil);
}
