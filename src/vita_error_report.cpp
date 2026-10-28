#include "vita_error_report.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __vita__
#include <malloc.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/appmgr.h>
#include <vitaGL.h>
#include "native_launcher/release.h"
#endif

namespace {
constexpr size_t limit = 32768;
char report[limit];
size_t length;
bool truncated;
char folder[512];
int rgss;
unsigned long long started;
struct Event {
    unsigned long long time;
    char phase[32], file[256], decoder[64];
    unsigned width, height;
    size_t compressed;
    unsigned long long decoded;
    long arena, used, free, blocks, top;
    int kernelResult, userFree, cdramFree, phyFree;
    unsigned long vramFree, ramFree, slowFree, budgetFree;
};
Event events[16];
unsigned eventCount;
unsigned graphicsRecoveryCount;
struct { char stage[64]; unsigned width, height; size_t compressed; } image;
unsigned long long now() {
#ifdef __vita__
    return sceKernelGetProcessTimeWide();
#else
    return static_cast<unsigned long long>(std::clock()) * 1000000 / CLOCKS_PER_SEC;
#endif
}
void memory(Event &e) {
#ifdef __vita__
    const struct mallinfo heap = mallinfo();
    e.arena = heap.arena; e.used = heap.uordblks; e.free = heap.fordblks;
    e.blocks = heap.ordblks; e.top = heap.keepcost;
    SceKernelFreeMemorySizeInfo info = {};
    info.size = sizeof(info);
    e.kernelResult = sceKernelGetFreeMemorySize(&info);
    e.userFree = info.size_user; e.cdramFree = info.size_cdram; e.phyFree = info.size_phycont;
    e.vramFree = vglMemFree(VGL_MEM_VRAM); e.ramFree = vglMemFree(VGL_MEM_RAM);
    e.slowFree = vglMemFree(VGL_MEM_SLOW); e.budgetFree = vglMemFree(VGL_MEM_BUDGET);
#else
    e.kernelResult = -1;
#endif
}
}
#ifdef __vita__
// Defined with the bitmap code: CPU-composed bitmaps (count, bytes), read-back copies, cached text.
extern "C" void vitaBitmapMemoryFigures(size_t figures[4]);
#ifdef __vita__
extern "C" void vitaPixelBlockFigures(uint32_t *count, uint32_t *bytes);
extern "C" void vitaPixelPoolFigures(uint32_t *count, uint32_t *bytes);
extern "C" void vitaTexPoolFigures(size_t figures[3], char *top, size_t capacity);
#endif
#endif
// Called only by vitaGL's exceptional allocation recovery path.
extern "C" void mkxp_gpu_allocation_recovery(void) { ++graphicsRecoveryCount; }
size_t vitaMemorySnapshot(char *destination, size_t capacity) {
    if (!capacity) return 0;
    Event e = {};
    memory(e);
    size_t pixels[4] = {0, 0, 0, 0};
#ifdef __vita__
    vitaBitmapMemoryFigures(pixels);
#endif
    uint32_t blockCount = 0, blockBytes = 0, poolCount = 0, poolBytes = 0;
    size_t targets[3] = {0, 0, 0};
    char targetTop[160] = "";
#ifdef __vita__
    vitaPixelBlockFigures(&blockCount, &blockBytes);
    vitaPixelPoolFigures(&poolCount, &poolBytes);
    vitaTexPoolFigures(targets, targetTop, sizeof(targetTop));
#endif
    const unsigned user = static_cast<unsigned>(e.userFree);
    const int userValid = e.kernelResult == 0 && user <= 512u * 1024 * 1024;
    const int count = snprintf(destination, capacity,
        "newlib_arena=%ld used=%ld free=%ld free_blocks=%ld top_free=%ld "
        "kernel_result=%d kernel_user_raw=%u kernel_user_valid=%d "
        "cdram_free=%u phycont_free=%u vgl_vram_free=%lu vgl_ram_free=%lu "
        "vgl_slow_free=%lu vgl_budget_free=%lu image_events=%u graphics_gc_cycles=%u "
        "cpu_bitmaps=%u cpu_bitmap_bytes=%u readback_bytes=%u text_cache_bytes=%u "
        "pixel_blocks=%u pixel_block_bytes=%u uncached_pixels=%u uncached_pixel_bytes=%u targets=%u target_bytes=%u pool_cache_bytes=%u target_top=%s last_image_phase=%s",
        e.arena, e.used, e.free, e.blocks, e.top, e.kernelResult, user, userValid,
        static_cast<unsigned>(e.cdramFree), static_cast<unsigned>(e.phyFree),
        e.vramFree, e.ramFree, e.slowFree, e.budgetFree, eventCount, graphicsRecoveryCount,
        static_cast<unsigned>(pixels[0]), static_cast<unsigned>(pixels[1]),
        static_cast<unsigned>(pixels[2]), static_cast<unsigned>(pixels[3]),
        static_cast<unsigned>(blockCount), static_cast<unsigned>(blockBytes),
        static_cast<unsigned>(poolCount), static_cast<unsigned>(poolBytes),
        static_cast<unsigned>(targets[0]), static_cast<unsigned>(targets[1]),
        static_cast<unsigned>(targets[2]), targetTop,
        eventCount ? events[(eventCount - 1) % 16].phase : "none");
    return count > 0 ? std::min<size_t>(count, capacity - 1) : 0;
}
// Optional SDL decoder callback. It neither allocates nor calls Ruby and is
// invoked only when decoding fails, before its temporary buffers are freed.
extern "C" void vitaImageFailure(const char *stage, unsigned width, unsigned height, size_t compressed) {
    snprintf(image.stage, sizeof(image.stage), "%s", stage);
    image.width = width; image.height = height; image.compressed = compressed;
    vitaErrorMemoryEvent("decoder-failure", "(see bitmap retry entry)");
}
void vitaErrorSessionStart(const char *gameFolder, int version) {
    snprintf(folder, sizeof(folder), "%s", gameFolder ? gameFolder : "?");
    rgss = version; started = now(); eventCount = 0; image = {};
}
void vitaErrorImageStart() { image = {}; }
void vitaErrorMemoryEvent(const char *phase, const char *filename) {
    Event &e = events[eventCount++ % 16];
    e = {}; e.time = now() - started;
    snprintf(e.phase, sizeof(e.phase), "%s", phase);
    snprintf(e.file, sizeof(e.file), "%s", filename ? filename : "?");
    snprintf(e.decoder, sizeof(e.decoder), "%s", image.stage);
    e.width = image.width; e.height = image.height; e.compressed = image.compressed;
    e.decoded = static_cast<unsigned long long>(e.width) * e.height * 4;
    memory(e);
}
void vitaErrorReportAppend(const char *text, size_t size) {
    const size_t copied = std::min(size, limit - length);
    memcpy(report + length, text, copied); length += copied;
    if (copied < size) truncated = true;
}
void vitaErrorReportFormat(const char *format, ...) {
    char line[2048];
    va_list args; va_start(args, format);
    int count = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (count > 0) vitaErrorReportAppend(line, std::min<size_t>(count, sizeof(line) - 1));
    if (count >= static_cast<int>(sizeof(line))) truncated = true;
}
void vitaErrorReportBegin() {
    length = 0; truncated = false;
#ifdef __vita__
    vitaErrorReportFormat("Reporter: 2 (failure-only)\nCapture runtime: HardRPG %s build %s\n", HARDRPG_VERSION, MKXPZ_GIT_HASH);
#endif
    vitaErrorReportFormat("Game root: %s\nRGSS: %d\nSession age: %.3f seconds\n", folder, rgss, (now() - started) / 1000000.0);
    vitaErrorReportFormat("Graphics allocation recovery cycles (process lifetime): %u\n", graphicsRecoveryCount);
#ifdef __vita__
    {
        size_t pixels[4] = {0, 0, 0, 0};
        uint32_t blockCount = 0, blockBytes = 0;
        vitaBitmapMemoryFigures(pixels);
        vitaPixelBlockFigures(&blockCount, &blockBytes);
        vitaErrorReportFormat("Bitmap memory: cpu_bitmaps=%u cpu_bitmap_bytes=%u readback_bytes=%u text_cache_bytes=%u pixel_blocks=%u pixel_block_bytes=%u\n",
                              static_cast<unsigned>(pixels[0]), static_cast<unsigned>(pixels[1]),
                              static_cast<unsigned>(pixels[2]), static_cast<unsigned>(pixels[3]),
                              static_cast<unsigned>(blockCount), static_cast<unsigned>(blockBytes));
    }
#endif
    vitaErrorMemoryEvent("terminal-exception", "");
    vitaErrorReportFormat("\nNative allocation history (last %u of %u failure/recovery events):\n", std::min(eventCount, 16u), eventCount);
    const unsigned first = eventCount > 16 ? eventCount - 16 : 0;
    for (unsigned i = first; i < eventCount; ++i) {
        const Event &e = events[i % 16];
        vitaErrorReportFormat("t=%.3f phase=%s image=%s\n decoder=%s width=%u height=%u compressed_bytes=%llu decoded_rgba_bytes=%llu\n newlib_arena=%ld used=%ld free=%ld free_blocks=%ld top_free=%ld\n kernel_result=%d user_free=%d cdram_free=%d phycont_free=%d\n vgl_free: vram=%lu ram=%lu slow=%lu budget=%lu\n", e.time / 1000000.0, e.phase, e.file, e.decoder, e.width, e.height, static_cast<unsigned long long>(e.compressed), e.decoded, e.arena, e.used, e.free, e.blocks, e.top, e.kernelResult, e.userFree, e.cdramFree, e.phyFree, e.vramFree, e.ramFree, e.slowFree, e.budgetFree);
    }
    vitaErrorReportFormat("Memory values are bytes. Newlib and kernel/vitaGL pools are distinct; totals do not measure the largest allocatable block.\n\n");
}
bool vitaErrorReportSave(const char *directory) {
    char path[1024], temp[1032], previous[1032];
    int count = snprintf(path, sizeof(path), "%s/last-error.txt", directory);
    if (count < 0 || count >= static_cast<int>(sizeof(path))) return false;
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    snprintf(previous, sizeof(previous), "%s.prev", path);
    mkdir(directory, 0777);
    const int file = open(temp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file < 0) return false;
    auto writeBytes = [file](const char *data, size_t size) {
        while (size) {
            const ssize_t count = write(file, data, size);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            data += count; size -= count;
        }
        return true;
    };
    bool ok = writeBytes(report, length);
    if (truncated) {
        const char suffix[] = "\n[Report truncated]\n";
        ok = writeBytes(suffix, sizeof(suffix) - 1) && ok;
    }
    if (close(file) != 0) ok = false;
    if (!ok) { std::remove(temp); return false; }
    // Every HardRPG app (release, test builds) shares this folder. The owner
    // file names the app whose game failed, so only that app's launcher shows
    // the report; the others only add it to the error history.
    char owner[1040], ownerTemp[1048];
    snprintf(owner, sizeof(owner), "%s.owner", path);
    snprintf(ownerTemp, sizeof(ownerTemp), "%s.tmp", owner);
    std::remove(owner);
#ifdef __vita__
    char title[16] = {};
    if (sceAppMgrAppParamGetString(0, 12, title, sizeof(title)) >= 0 && title[0]) {
        const int ownerFile = open(ownerTemp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (ownerFile >= 0) {
            bool written = write(ownerFile, title, strlen(title)) == static_cast<ssize_t>(strlen(title));
            if (close(ownerFile) != 0 || !written || std::rename(ownerTemp, owner) != 0)
                std::remove(ownerTemp);
        }
    }
#endif
    struct stat old;
    if (stat(path, &old) == 0) {
        std::remove(previous);
        if (std::rename(path, previous) != 0) return false;
    }
    return std::rename(temp, path) == 0;
}
bool vitaWriteErrorReport(const std::string &text, const char *directory) {
    length = 0; truncated = false;
    vitaErrorReportAppend(text.data(), text.size());
    return vitaErrorReportSave(directory);
}
bool vitaWriteFileReplacing(const char *path, const char *data, size_t size) {
    char temp[1040];
    const int count = snprintf(temp, sizeof(temp), "%s.tmp", path);
    if (count < 0 || count >= static_cast<int>(sizeof(temp))) return false;
    const int file = open(temp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file < 0) return false;
    bool ok = true;
    while (size && ok) {
        const ssize_t written = write(file, data, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) ok = false;
        else { data += written; size -= written; }
    }
    if (close(file) != 0) ok = false;
    // The Vita's rename does not replace an existing file.
    if (ok) { std::remove(path); ok = std::rename(temp, path) == 0; }
    if (!ok) std::remove(temp);
    return ok;
}
