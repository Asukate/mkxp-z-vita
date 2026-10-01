/*
 * Vita-only startup and crash diagnostics.
 */

#include "vita_diagnostic.h"

#ifdef MKXPZ_VITA_DIAGNOSTICS

#include <SDL_gamecontroller.h>
#include <SDL_timer.h>
#include "frame_profile.h"
namespace FrameProfile {
State state = {};
uint64_t clock() { return SDL_GetPerformanceCounter(); }
}

extern "C" int mkxp_upload_profile_enter(unsigned id, uint64_t bytes) {
    return FrameProfile::enter(id, bytes);
}
extern "C" void mkxp_upload_profile_leave() { FrameProfile::leave(); }

#include <vitaGL.h>

#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

/* Logical storage owned through mkxp-z's TEX wrapper, not allocator residency.
 * Replacement updates a slot; subimage uploads do not add live storage.
 * Delayed vitaGL frees and COW backing are intentionally measured separately. */
static uint64_t gpuLogicalBytes[16384] = {};
static uint64_t gpuUploadBytes = 0;
static uint64_t gpuLiveBytes = 0;
static unsigned int gpuLiveTextures = 0;
static unsigned int gpuUntrackedIds = 0;

void vitaDiagTextureStorage(unsigned int id, int width, int height)
{
    if (!id || id >= 16384) { ++gpuUntrackedIds; return; }
    uint64_t bytes = uint64_t(width > 0 ? width : 0) *
                     uint64_t(height > 0 ? height : 0) * 4;
    gpuUploadBytes += bytes;
    gpuLiveBytes -= gpuLogicalBytes[id];
    if (gpuLogicalBytes[id]) --gpuLiveTextures;
    gpuLogicalBytes[id] = bytes;
    gpuLiveBytes += bytes;
    if (bytes) ++gpuLiveTextures;
}

void vitaDiagTextureDelete(unsigned int id)
{
    if (!id || id >= 16384) return;
    gpuLiveBytes -= gpuLogicalBytes[id];
    if (gpuLogicalBytes[id]) --gpuLiveTextures;
    gpuLogicalBytes[id] = 0;
}

unsigned long long vitaDiagTextureUploadBytes() { return gpuUploadBytes; }

extern "C" int frame_elem_purge_idx;
extern "C" int frame_rt_purge_idx;
extern "C" int frame_purge_idx;

void vitaDiagGpuSnapshot(const char *phase)
{
    vitaDiagLog("GPUGC", "snapshot=%s logical_textures=%u logical_rgba_bytes=%llu "
                "upload_bytes=%llu untracked_ids=%u pending_current_slot=%d "
                "pending_current_rt=%d purge_slot=%d", phase, gpuLiveTextures,
                (unsigned long long)gpuLiveBytes, (unsigned long long)gpuUploadBytes,
                gpuUntrackedIds, frame_elem_purge_idx, frame_rt_purge_idx, frame_purge_idx);
    vitaDiagLogVglMemory(phase);
}

extern "C" {

/*
 * S42-E low-perturbation crash flight recorder.
 *
 * S42-D proved that the original zero-initialized .bss page was not present in
 * the Vita psp2core dump.  Keep the same 32-byte record format, but force the
 * ring into a page-aligned, initialized .data subsection.  The non-zero
 * reserved value in record zero is only a linker/data anchor; the first real
 * breadcrumb overwrites it and publishes magic last.
 */
struct S42DBreadcrumb
{
    uint32_t magic;
    uint32_t seq;
    uint64_t timeUs;
    int32_t threadId;
    uint32_t stage;
    uintptr_t caller;
    uint32_t reserved;
};

volatile uint32_t g_s42d_breadcrumb_next = 0;
volatile S42DBreadcrumb g_s42d_breadcrumb_ring[128]
    __attribute__((section(".data.s42e"), aligned(4096), used)) = {
        {0u, 0u, 0u, 0, 0u, 0u, 0xE42E42E1u}
    };

} // extern "C"

namespace {

constexpr const char *kLogPath =
    "ux0:/data/hardrpg/mkxp-z-vita-diagnostic.log";
constexpr const char *kDumpTriggerPath =
    "ux0:/data/hardrpg/trigger-psp2dump";

constexpr uint32_t kS42DBreadcrumbMagic = 0x44323453u; /* "S42D" LE */

enum S42DBreadcrumbStage : uint32_t
{
    S42D_TMPREP_BEGIN        = 0x100,
    S42D_TMPREP_DONE         = 0x101,
    S42D_TMDRAW_FRAME_DONE   = 0x110,
    S42D_FLASH_PREP_ENTER    = 0x200,
    S42D_FLASH_CLEAN_RETURN  = 0x201,
    S42D_FLASH_REBUILD_BEGIN = 0x202,
    S42D_FLASH_REBUILD_DONE  = 0x203,
    S42D_FLASH_PREP_DONE     = 0x204,

    S42E_ATLAS_BUILD_BEGIN   = 0x300,
    S42E_ATLAS_BLITBEGIN_DONE= 0x301,
    S42E_ATLAS_CLEAR_DONE    = 0x302,
    S42E_ATLAS_SHADOW_DONE   = 0x303,
    S42E_ATLAS_A1_BEGIN      = 0x310,
    S42E_ATLAS_A1_DONE       = 0x311,
    S42E_ATLAS_A2_BEGIN      = 0x320,
    S42E_ATLAS_A2_DONE       = 0x321,
    S42E_ATLAS_A3_BEGIN      = 0x330,
    S42E_ATLAS_A3_DONE       = 0x331,
    S42E_ATLAS_A4_BEGIN      = 0x340,
    S42E_ATLAS_A4_DONE       = 0x341,
    S42E_ATLAS_A5_BEGIN      = 0x350,
    S42E_ATLAS_A5_DONE       = 0x351,
    S42E_ATLAS_B_BEGIN       = 0x360,
    S42E_ATLAS_B_DONE        = 0x361,
    S42E_ATLAS_C_BEGIN       = 0x370,
    S42E_ATLAS_C_DONE        = 0x371,
    S42E_ATLAS_D_BEGIN       = 0x380,
    S42E_ATLAS_D_DONE        = 0x381,
    S42E_ATLAS_E_BEGIN       = 0x390,
    S42E_ATLAS_E_DONE        = 0x391,
    S42E_ATLAS_BLITEND_BEGIN = 0x3A0,
    S42E_ATLAS_BUILD_DONE    = 0x3A1
};

FILE *logFile = nullptr;
volatile int logLock = 0;
uint32_t controllerButtons = 0;
bool dumpTriggered = false;

/*
 * S42-G: S42-F proved that the A2 blit and an immediate sceGxmFinish fence
 * both complete, but all normal TileAtlas/Tilemap handoff milestones were
 * still diverted into the psp2core-invisible RAM ring.  Re-use those existing
 * markers rather than adding renderer code: arm only on the second VX atlas
 * build, mirror renderer milestones to the normal log with an immediate
 * fflush, then stop once a complete tilemap draw has crossed into the next
 * frame's prepare call.  This is a short one-shot trace, not per-frame spam.
 */
unsigned int s42gAtlasBuildCount = 0;
bool s42gTraceActive = false;
bool s42gSawFrameDone = false;

void s42dWriteBreadcrumb(uint32_t stage, uintptr_t caller)
{
    const uint32_t seq = __sync_fetch_and_add(&g_s42d_breadcrumb_next, 1u);
    volatile S42DBreadcrumb &rec = g_s42d_breadcrumb_ring[seq & 127u];

    /* Invalidate first, populate payload, then publish magic last. */
    rec.magic = 0;
    rec.seq = seq;
    rec.timeUs = static_cast<uint64_t>(sceKernelGetSystemTimeWide());
    rec.threadId = static_cast<int32_t>(sceKernelGetThreadId());
    rec.stage = stage;
    rec.caller = caller;
    rec.reserved = 0;
    __sync_synchronize();
    rec.magic = kS42DBreadcrumbMagic;
}

uint32_t s42dClassifyBreadcrumb(const char *tag, const char *format)
{
    if (!tag || !format)
        return 0;

    if (std::strcmp(tag, "TMPREP") == 0)
    {
        if (std::strcmp(format, "prepare begin atlasDirty=%d buffersDirty=%d") == 0)
            return S42D_TMPREP_BEGIN;
        if (std::strcmp(format, "prepare done") == 0)
            return S42D_TMPREP_DONE;
        return 0;
    }

    if (std::strcmp(tag, "TMDRAW") == 0)
    {
        if (std::strcmp(format, "draw flash done") == 0)
            return S42D_TMDRAW_FRAME_DONE;
        return 0;
    }

    if (std::strcmp(tag, "S42C-FLASH") == 0)
    {
        if (std::strncmp(format, "prepare_enter ", 14) == 0)
            return S42D_FLASH_PREP_ENTER;
        if (std::strncmp(format, "clean_return ", 13) == 0)
            return S42D_FLASH_CLEAN_RETURN;
        if (std::strncmp(format, "rebuild_begin ", 14) == 0)
            return S42D_FLASH_REBUILD_BEGIN;
        if (std::strncmp(format, "rebuild_done ", 13) == 0)
            return S42D_FLASH_REBUILD_DONE;
        if (std::strncmp(format, "prepare_done ", 13) == 0)
            return S42D_FLASH_PREP_DONE;
        return 0;
    }

    if (std::strcmp(tag, "ATLAS") == 0)
    {
        if (std::strcmp(format, "build begin tf=%ux%u") == 0)
            return S42E_ATLAS_BUILD_BEGIN;
        if (std::strcmp(format, "after blitBegin") == 0)
            return S42E_ATLAS_BLITBEGIN_DONE;
        if (std::strcmp(format, "after clear") == 0)
            return S42E_ATLAS_CLEAR_DONE;
        if (std::strcmp(format, "after shadow upload") == 0)
            return S42E_ATLAS_SHADOW_DONE;
        if (std::strcmp(format, "before blits A1") == 0)
            return S42E_ATLAS_A1_BEGIN;
        if (std::strcmp(format, "after blits A1") == 0)
            return S42E_ATLAS_A1_DONE;
        if (std::strcmp(format, "before blits A2") == 0)
            return S42E_ATLAS_A2_BEGIN;
        if (std::strcmp(format, "after blits A2") == 0)
            return S42E_ATLAS_A2_DONE;
        if (std::strcmp(format, "before blits A3") == 0)
            return S42E_ATLAS_A3_BEGIN;
        if (std::strcmp(format, "after blits A3") == 0)
            return S42E_ATLAS_A3_DONE;
        if (std::strcmp(format, "before blits A4") == 0)
            return S42E_ATLAS_A4_BEGIN;
        if (std::strcmp(format, "after blits A4") == 0)
            return S42E_ATLAS_A4_DONE;
        if (std::strcmp(format, "before blits A5") == 0)
            return S42E_ATLAS_A5_BEGIN;
        if (std::strcmp(format, "after blits A5") == 0)
            return S42E_ATLAS_A5_DONE;
        if (std::strcmp(format, "before blits B") == 0)
            return S42E_ATLAS_B_BEGIN;
        if (std::strcmp(format, "after blits B") == 0)
            return S42E_ATLAS_B_DONE;
        if (std::strcmp(format, "before blits C") == 0)
            return S42E_ATLAS_C_BEGIN;
        if (std::strcmp(format, "after blits C") == 0)
            return S42E_ATLAS_C_DONE;
        if (std::strcmp(format, "before blits D") == 0)
            return S42E_ATLAS_D_BEGIN;
        if (std::strcmp(format, "after blits D") == 0)
            return S42E_ATLAS_D_DONE;
        if (std::strcmp(format, "before blits E") == 0)
            return S42E_ATLAS_E_BEGIN;
        if (std::strcmp(format, "after blits E") == 0)
            return S42E_ATLAS_E_DONE;
        if (std::strcmp(format, "before blitEnd") == 0)
            return S42E_ATLAS_BLITEND_BEGIN;
        if (std::strcmp(format, "build done") == 0)
            return S42E_ATLAS_BUILD_DONE;
        return 0;
    }

    return 0;
}

void lockLog()
{
    while (__sync_lock_test_and_set(&logLock, 1))
        ;
}

void unlockLog()
{
    __sync_lock_release(&logLock);
}

void ensureDirectories()
{
    if (mkdir("ux0:/data", 0777) < 0 && errno != EEXIST)
        sceClibPrintf("[MKXPZ-VITA] mkdir ux0:/data failed: %d\n", errno);

    if (mkdir("ux0:/data/hardrpg", 0777) < 0 && errno != EEXIST)
        sceClibPrintf("[MKXPZ-VITA] mkdir diagnostic directory failed: %d\n",
                      errno);
}

void triggerDump(const char *reason)
{
    if (dumpTriggered)
        return;

    dumpTriggered = true;
    vitaDiagLog("DUMP", "deliberate fault requested reason=%s", reason);

    /* A real Vita's crash handler should turn this into a psp2dump. */
    volatile uint32_t *faultAddress =
        reinterpret_cast<volatile uint32_t *>(static_cast<uintptr_t>(1));
    *faultAddress = 0x4d4b5850u;
}

} // namespace

static bool quietDiagnostics = false;
static bool textProfileEnabled = false;
static bool atlasTraceEnabled = false;

bool vitaDiagTextProfileEnabled() { return textProfileEnabled; }
bool vitaDiagAtlasTraceEnabled() { return atlasTraceEnabled; }

void vitaDiagInit(const char *argv0)
{
    quietDiagnostics = access("app0:/quiet-diagnostics.on", F_OK) == 0;
    atlasTraceEnabled = access("app0:/atlas-trace.on", F_OK) == 0;
    textProfileEnabled = access("app0:/text-profile.on", F_OK) == 0;
    ensureDirectories();
    /* Vita: the diagnostic log is append-only and shared across sessions;
     * verbose builds grew it past 150MB, making evidence pulls impractical
     * (no ranged reads; timeouts must not be retried). Rotate: start fresh
     * when stale content exceeds 32MB so one session stays pullable. */
    {
        FILE *probe = std::fopen(kLogPath, "rb");
        if (probe) {
            std::fseek(probe, 0, SEEK_END);
            long size = std::ftell(probe);
            std::fclose(probe);
            if (size > 33554432)
                logFile = std::fopen(kLogPath, "wb");
        }
    }
    if (!logFile)
        logFile = std::fopen(kLogPath, "ab");

    vitaDiagLog("START", "argv0=%s pid=%d tid=%d log=%s",
                argv0 ? argv0 : "<null>",
                static_cast<int>(sceKernelGetProcessId()),
                static_cast<int>(sceKernelGetThreadId()), kLogPath);
    vitaDiagLog("BUILD", "diagnostic=1 rgss_stack=5MiB dump_trigger=SELECT-or-file git=%s",
#ifdef MKXPZ_GIT_HASH
                MKXPZ_GIT_HASH
#else
                "nogit"
#endif
                );
    vitaDiagLogThread("main_start");

    if (access(kDumpTriggerPath, F_OK) == 0) {
        unlink(kDumpTriggerPath);
        triggerDump("trigger-psp2dump file");
    }
}

void vitaDiagShutdown()
{
    vitaDiagLog("END", "normal shutdown");

    lockLog();
    if (logFile) {
        std::fclose(logFile);
        logFile = nullptr;
    }
    unlockLog();
}

void vitaDiagLog(const char *tag, const char *format, ...)
{
    // Atlas forensic reads and their output are an explicit package opt-in.
    if (tag && (std::strcmp(tag, "TMXP") == 0 || std::strcmp(tag, "ATLAS") == 0)
        && !atlasTraceEnabled)
        return;
    // A package-local performance gate overrides old device-wide trace flags.
    // Keep startup, failure and asset-origin evidence without per-draw I/O.
    if (quietDiagnostics && (!tag || (std::strcmp(tag, "TEXTTIME") != 0 && std::strcmp(tag, "START") != 0 &&
                          std::strcmp(tag, "END") != 0 &&
                          std::strcmp(tag, "ERROR") != 0 &&
                          std::strcmp(tag, "CONTENT") != 0 &&
                          std::strcmp(tag, "DEBUG-aoni") != 0 &&
                          std::strcmp(tag, "ATLAS") != 0 &&
                          std::strcmp(tag, "BUILD") != 0 &&
                          std::strcmp(tag, "HARNESS") != 0 &&
                          std::strcmp(tag, "TMXP") != 0 &&
                          std::strcmp(tag, "BOOTPERF") != 0)))
        return;
    const bool textTraceTag = tag &&
        (std::strcmp(tag, "TEXTFLOW") == 0 ||
         std::strcmp(tag, "TTF") == 0);
    static int textTraceEnabled = -1;
    if (textTraceTag && textTraceEnabled < 0)
        textTraceEnabled =
            access("ux0:/data/hardrpg/enable-text-trace", F_OK) == 0;
    if (textTraceTag && !textTraceEnabled)
        return;

    /* Historical GPU probes remain available for a targeted repro, but they
     * must not run during performance testing.  These tags are emitted from
     * per-frame/per-draw paths and sceClibPrintf alone is enough to perturb
     * scheduling even when the file is flushed only periodically. */
    static const char *const kHotTraceTags[] = {
        "SWAP", "AXREC", "SETBITMAP", "S27", "S27-EVENT", "VAO",
        "FONT", "S36-SPR", "S38-QUAD", "S39-TEX", "S34-GEO", "S33",
        "S33-ELEM", "S35-ELEM", "S37-PROJ", "S37-CLIP", "S36-PERF",
        "FRAME", "S42B-BH", nullptr
    };
    bool hotTraceTag = false;
    if (tag) {
        for (int i = 0; kHotTraceTags[i]; ++i) {
            if (std::strcmp(tag, kHotTraceTags[i]) == 0) {
                hotTraceTag = true;
                break;
            }
        }
    }
    static int hotTraceEnabled = -1;
    if (hotTraceTag && hotTraceEnabled < 0)
        hotTraceEnabled =
            access("ux0:/data/hardrpg/enable-hot-trace", F_OK) == 0;
    if (hotTraceTag && !hotTraceEnabled)
        return;

    const bool rendererTag = tag &&
        (std::strcmp(tag, "TMPREP") == 0 ||
         std::strcmp(tag, "TMDRAW") == 0 ||
         std::strcmp(tag, "S42C-FLASH") == 0 ||
         std::strcmp(tag, "ATLAS") == 0);

    bool s42gMirrorRendererTag = false;
    bool s42gMarkFrameDoneAfterWrite = false;
    bool s42gDeactivateAfterWrite = false;

    /*
     * S42-E normally keeps high-volume renderer markers out of stdio and sends
     * selected boundaries only to the flight recorder.  S42-G preserves that
     * default, but mirrors the EXISTING markers during one short, deterministic
     * handoff window: atlas build #2 through the first complete tilemap draw
     * and into the next frame's prepare call.  Mirrored lines are force-flushed
     * below so their ordering is crash-precise.
     */
    if (rendererTag)
    {
        const uint32_t stage = s42dClassifyBreadcrumb(tag, format);
        if (stage)
        {
            const uintptr_t caller =
                reinterpret_cast<uintptr_t>(__builtin_return_address(0));
            s42dWriteBreadcrumb(stage, caller);
        }

        if (std::strcmp(tag, "ATLAS") == 0 && format &&
            std::strcmp(format, "build begin tf=%ux%u") == 0)
        {
            ++s42gAtlasBuildCount;
            if (s42gAtlasBuildCount == 2)
            {
                s42gTraceActive = true;
                s42gSawFrameDone = false;
            }
        }

        if (!s42gTraceActive)
            return;

        s42gMirrorRendererTag = true;

        if (s42gSawFrameDone && std::strcmp(tag, "TMPREP") == 0 && format &&
            std::strcmp(format,
                        "prepare begin atlasDirty=%d buffersDirty=%d") == 0)
        {
            /* Mirror this next-frame entry, then return to ring-only mode. */
            s42gDeactivateAfterWrite = true;
        }

        if (std::strcmp(tag, "TMDRAW") == 0 && format &&
            std::strcmp(format, "draw flash done") == 0)
        {
            s42gMarkFrameDoneAfterWrite = true;
        }
    }

    /* S35: per-call noise tags are suppressed — they destroyed frame rate
     * (fflush per line at title = ~1fps). Keep one-shot/lifecycle/error
     * markers only. */
    static const char *const kQuietTags[] = {
        "TEXUP", "SHADER", "BITMAP", "MEMORY", "VGLMEM", "VGLPOOL",
        "BINDING", "DEBUG-FBO-20260804", "GUPDATELIM", "GUPDATE",
        "TRANS", "MEMBER", "VGLHDR", "DRAW", "THREAD", "BUILD",
        nullptr
    };
    if (tag) {
        for (int i = 0; kQuietTags[i]; ++i) {
            if (std::strcmp(tag, kQuietTags[i]) == 0)
                return;
        }
    }

    char message[768];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    message[sizeof(message) - 1] = '\0';

    const long long timeUs = static_cast<long long>(sceKernelGetSystemTimeWide());
    const int threadId = static_cast<int>(sceKernelGetThreadId());

    lockLog();
    if (logFile) {
        std::fprintf(logFile, "%lld tid=%d [%s] %s\n", timeUs, threadId,
                     tag ? tag : "LOG", message);
        if ((tag && std::strcmp(tag, "TEXTFLOW") == 0) ||
            (tag && std::strcmp(tag, "ERROR") == 0) ||
            (tag && std::strcmp(tag, "SCENE") == 0) ||
            s42gMirrorRendererTag)
            std::fflush(logFile);
        /* S35: no per-line fflush outside TEXTFLOW/S42-G's one-shot window. */
        static int flushCounter = 0;
        if ((++flushCounter & 63) == 0)
            std::fflush(logFile);
    }
    sceClibPrintf("[MKXPZ-VITA][%s] %s\n", tag ? tag : "LOG", message);
    unlockLog();

    if (s42gMarkFrameDoneAfterWrite)
        s42gSawFrameDone = true;

    if (s42gDeactivateAfterWrite)
        s42gTraceActive = false;
}

void vitaDiagLogThread(const char *phase)
{
    const int threadId = static_cast<int>(sceKernelGetThreadId());
    const int freeStack = sceKernelGetThreadStackFreeSize(threadId);
    vitaDiagLog("THREAD", "phase=%s tid=%d stack_free=%d",
                phase ? phase : "<null>", threadId, freeStack);
}

void vitaDiagLogMemory(const char *phase)
{
    SceKernelFreeMemorySizeInfo info = {};
    info.size = sizeof(info);
    const int result = sceKernelGetFreeMemorySize(&info);
    vitaDiagLog("MEMMARK",
                "phase=%s result=0x%08x user_free=%d cdram_free=%d phycont_free=%d",
                phase ? phase : "<null>", result, info.size_user,
                info.size_cdram, info.size_phycont);
    vitaDiagLog("MEMMARK",
                "phase=%s-pools vram=%lu/%lu ram=%lu/%lu slow=%lu/%lu budget=%lu/%lu",
                phase ? phase : "<null>",
                static_cast<unsigned long>(vglMemFree(VGL_MEM_VRAM)),
                static_cast<unsigned long>(vglMemTotal(VGL_MEM_VRAM)),
                static_cast<unsigned long>(vglMemFree(VGL_MEM_RAM)),
                static_cast<unsigned long>(vglMemTotal(VGL_MEM_RAM)),
                static_cast<unsigned long>(vglMemFree(VGL_MEM_SLOW)),
                static_cast<unsigned long>(vglMemTotal(VGL_MEM_SLOW)),
                static_cast<unsigned long>(vglMemFree(VGL_MEM_BUDGET)),
                static_cast<unsigned long>(vglMemTotal(VGL_MEM_BUDGET)));
}

void vitaDiagLogVglPool(const char *phase)
{
#ifdef MKXPZ_VITA_DIAGNOSTICS
    extern uint8_t *circular_data_pool[];
    extern uint8_t *circular_data_pool_ptr[];
    extern int vgl_circular_idx;
    for (int i = 0; i < 3; ++i)
    {
        vitaDiagLog("VGLPOOL",
                    "phase=%s buf=%d base=%p ptr=%p limit=%p used=%lu",
                    phase ? phase : "<null>", i,
                    (void *)circular_data_pool[i],
                    (void *)circular_data_pool_ptr[i],
                    (void *)(circular_data_pool[i] +
                             (circular_data_pool_ptr[i] -
                              circular_data_pool[i])),
                    (unsigned long)(circular_data_pool_ptr[i] -
                                    circular_data_pool[i]));
    }
    vitaDiagLog("VGLPOOL", "phase=%s idx=%d", phase ? phase : "<null>",
                vgl_circular_idx);
#else
    (void) phase;
#endif
}

void vitaDiagLogVglMemory(const char *phase)
{
#ifdef MKXPZ_VITA_DIAGNOSTICS
    // Heap statistics can walk allocator state. Quiet tracing must skip the
    // query itself, not merely discard its formatted output afterwards.
    if (quietDiagnostics) return;
    FrameProfile::Scope profile(FrameProfile::Diagnostics);
    vitaDiagLog(
        "VGLMEM",
        "phase=%s vram_free=%lu/%lu ram_free=%lu/%lu slow_free=%lu/%lu "
        "budget_free=%lu/%lu",
        phase ? phase : "<null>",
        static_cast<unsigned long>(vglMemFree(VGL_MEM_VRAM)),
        static_cast<unsigned long>(vglMemTotal(VGL_MEM_VRAM)),
        static_cast<unsigned long>(vglMemFree(VGL_MEM_RAM)),
        static_cast<unsigned long>(vglMemTotal(VGL_MEM_RAM)),
        static_cast<unsigned long>(vglMemFree(VGL_MEM_SLOW)),
        static_cast<unsigned long>(vglMemTotal(VGL_MEM_SLOW)),
        static_cast<unsigned long>(vglMemFree(VGL_MEM_BUDGET)),
        static_cast<unsigned long>(vglMemTotal(VGL_MEM_BUDGET)));
#else
    (void) phase;
#endif
}

void vitaDiagPollTrigger()
{
    if (!dumpTriggered && access(kDumpTriggerPath, F_OK) == 0) {
        unlink(kDumpTriggerPath);
        triggerDump("trigger-psp2dump file");
    }
}

void vitaDiagControllerButton(int button, bool pressed)
{
    if (button < 0 || button >= 32)
        return;

    const uint32_t bit = 1u << static_cast<unsigned int>(button);
    if (pressed)
        controllerButtons |= bit;
    else
        controllerButtons &= ~bit;

    if (!dumpTriggered && pressed && button == SDL_CONTROLLER_BUTTON_BACK)
        triggerDump("SELECT controller button");
}

#endif
