#!/usr/bin/env python3
"""Exercise the real bitmap bindings' bounded memory recovery boundary."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / 'binding/bitmap-binding.cpp').read_text()
start = source.index('#ifdef __vita__\n// The image decoder reports')
end = source.index('\nvoid bitmapInitProps(', start)
function = source[start:end]
test = r'''
#include <cassert>
#include <cstring>
#include "src/util/exception.h"
using VALUE = unsigned long;
static int calls, collections, parses, failures, cacheClears;
static bool graphicsLocked;
static bool cacheBlocksAllocation;
static Exception::Type failureType;
static const char *failureMessage;
static char originalName[] = "original";
static char movedName[] = "moved";
static char *currentName;
static const VALUE rb_cObject = 1;
static const VALUE gcModule = 2;
#define RB_ARG_END
void rb_get_args(int, VALUE *, const char *, char **name) {
    ++parses;
    *name = currentName;
}
VALUE rb_intern(const char *name) {
    return !strcmp(name, "GC") ? 3 : 4;
}
VALUE rb_const_get(VALUE module, VALUE name) {
    assert(module == rb_cObject && name == 3);
    return gcModule;
}
VALUE rb_funcall(VALUE receiver, VALUE method, int argc) {
    assert(receiver == gcModule && method == 4 && argc == 0);
    assert(!graphicsLocked);
    ++collections;
    currentName = movedName;
    return 0;
}
void vitaErrorImageStart() {}
static int memoryEvents;
void vitaErrorMemoryEvent(const char *, const char *) { assert(!graphicsLocked); ++memoryEvents; }
struct TestTexPool {
    void clearCache() { assert(graphicsLocked); ++cacheClears; cacheBlocksAllocation = false; }
};
struct TestSharedState {
    TestTexPool pool;
    TestTexPool &texPool() { return pool; }
};
static TestSharedState sharedState;
static TestSharedState *shState = &sharedState;
static int pixelReclaims = 0;
struct Bitmap {
    // Releasing re-creatable pixel copies happens with the pool cache, under the graphics lock.
    static void reclaimMemory() { assert(graphicsLocked); ++pixelReclaims; }
    explicit Bitmap(const char *name) {
        assert(graphicsLocked);
        assert(name == currentName);
        ++calls;
        if (cacheBlocksAllocation || failures-- > 0)
            throw Exception(failureType, "%s", failureMessage);
    }
    Bitmap(int, int) {
        assert(graphicsLocked);
        ++calls;
        if (cacheBlocksAllocation || failures-- > 0)
            throw Exception(failureType, "%s", failureMessage);
    }
};
#define GFX_GUARD_EXC(expression) { \
    assert(!graphicsLocked); graphicsLocked = true; \
    try { expression } catch (const Exception &) { \
        graphicsLocked = false; throw; \
    } \
    graphicsLocked = false; \
}
'''
test += function
test += r'''
static void reset(int count, Exception::Type type, const char *message) {
    calls = collections = parses = memoryEvents = cacheClears = 0;
    cacheBlocksAllocation = false;
    failures = count;
    failureType = type;
    failureMessage = message;
    currentName = originalName;
    graphicsLocked = false;
}
int main() {
    VALUE filename = 0;
    reset(0, Exception::SDLError, "");
    delete bitmapFromFile(1, &filename);
    assert(calls == 1 && collections == 0 && parses == 1 && memoryEvents == 0 && cacheClears == 0);
    reset(1, Exception::SDLError, "Error loading image 'test': out of memory allocating PNG pixels");
    delete bitmapFromFile(1, &filename);
    assert(calls == 2 && collections == 1 && parses == 2 && memoryEvents == 5 && cacheClears == 1);
    reset(0, Exception::SDLError, "Error loading image 'test': out of memory allocating PNG pixels");
    cacheBlocksAllocation = true;
    delete bitmapFromFile(1, &filename);
    assert(calls == 2 && collections == 1 && cacheClears == 1);
    reset(2, Exception::SDLError, "Error loading image 'test': out of memory reading image");
    try { delete bitmapFromFile(1, &filename); assert(false); }
    catch (const Exception &e) { assert(e.type == Exception::SDLError); }
    assert(calls == 2 && collections == 1 && cacheClears == 1 && !graphicsLocked);
    reset(1, Exception::SDLError, "Error loading image 'test': not a PNG");
    try { delete bitmapFromFile(1, &filename); assert(false); }
    catch (const Exception &) {}
    assert(calls == 1 && collections == 0 && cacheClears == 0 && !graphicsLocked);
    reset(1, Exception::NoFileError, "test: out of memory reading image");
    try { delete bitmapFromFile(1, &filename); assert(false); }
    catch (const Exception &e) { assert(e.type == Exception::NoFileError); }
    assert(calls == 1 && collections == 0 && cacheClears == 0 && !graphicsLocked);
    const char *texture = "Graphics memory: out of memory allocating 544x416 texture";
    reset(1, Exception::MKXPError, texture);
    delete bitmapFromFile(1, &filename);
    assert(calls == 2 && collections == 1 && cacheClears == 1 && !graphicsLocked);
    reset(0, Exception::MKXPError, texture);
    delete vitaBitmapOfSize(544, 416);
    assert(calls == 1 && collections == 0 && memoryEvents == 0 && cacheClears == 0);
    reset(1, Exception::MKXPError, texture);
    delete vitaBitmapOfSize(544, 416);
    assert(calls == 2 && collections == 1 && memoryEvents == 5 && cacheClears == 1);
    reset(0, Exception::MKXPError, texture);
    cacheBlocksAllocation = true;
    delete vitaBitmapOfSize(544, 416);
    assert(calls == 2 && collections == 1 && cacheClears == 1);
    reset(2, Exception::MKXPError, texture);
    try { delete vitaBitmapOfSize(544, 416); assert(false); }
    catch (const Exception &e) { assert(e.type == Exception::MKXPError); }
    assert(calls == 2 && collections == 1 && cacheClears == 1 && !graphicsLocked);
    reset(1, Exception::MKXPError, "Texture dimensions [9000, 9000] exceed hardware capabilities");
    try { delete vitaBitmapOfSize(9000, 9000); assert(false); }
    catch (const Exception &) {}
    assert(calls == 1 && collections == 0 && cacheClears == 0 && !graphicsLocked);
    puts("PASS: one retry only for image or texture allocation failure; GC outside graphics lock; filename refreshed; genuine errors preserved");
}
'''
with tempfile.TemporaryDirectory(prefix='hardrpg-bitmap-memory-') as temporary:
    path = Path(temporary)
    (path / 'check.cpp').write_text(test)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-D__vita__', '-I', str(root),
                    str(path / 'check.cpp'), '-o', str(path / 'check')], check=True)
    subprocess.run([str(path / 'check')], check=True)
