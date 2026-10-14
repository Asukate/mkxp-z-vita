#!/usr/bin/env python3
"""Exercise actual vitaGL target marking and texture release with pending writes.

Pass the pinned, patched vitaGL source tree used by the dependency build.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1])
gxm = (source / 'source/gxm.c').read_text()
gpu = (source / 'source/utils/gpu_utils.h').read_text()
shared = (source / 'source/shared.h').read_text()
macro_start = shared.index('#define mark_texture_used(tex)')
macro_end = shared.index('\n#endif', macro_start)
mark_macro = shared[macro_start:macro_end] + '\n'

def function(text, name):
    start = text.index(name)
    start = text.rfind('\n', 0, start) + 1
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

# This prefix runs for every clear/draw, including continuation of an open
# scene. It must mark the destination even when no new scene is started.
start = gxm.index('void scene_reset(void) {')
end = gxm.index('\n\tif (in_use_framebuffer', start)
mark = gxm[start:end] + '\n}\n'
release = function(gpu, 'void gpu_free_texture_data(')
preamble = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#define GL_TRUE true
#define OBJ_NOT_USED UINT32_MAX
#define FRAME_PURGE_FREQ 4
struct texture { void *data; void *palette_data; unsigned last_frame, last_finish_epoch; };
struct framebuffer { texture *tex; };
static unsigned vgl_framecount=30, vgl_finish_epoch=7;
static bool vgl_gpu_write_pending, freed, deferred;
static framebuffer *active_write_fb;
void vgl_free(void *) { freed=true; }
void mark_as_dirty(void *) { deferred=true; }
'''
checks = r'''
int main() {
 int pixels;
 texture target{&pixels,nullptr,OBJ_NOT_USED,0};
 framebuffer fb{&target};active_write_fb=&fb;
 scene_reset(); // An emitted clear owns the target backing store.
 gpu_free_texture_data(&target);
 if(freed || !deferred)return 1; // GPU could overwrite allocator free links.
 assert(target.data==nullptr);
 assert(target.last_frame==vgl_framecount && target.last_finish_epoch==vgl_finish_epoch);
 ++vgl_framecount; ++vgl_finish_epoch;
 target.data=&pixels;freed=deferred=false;
 scene_reset(); // Another emit in an already-open scene refreshes lifetime.
 gpu_free_texture_data(&target);
 assert(!freed && deferred && target.last_frame==vgl_framecount);
 active_write_fb=nullptr;scene_reset(); // Display framebuffer has no texture.
 framebuffer empty{nullptr};active_write_fb=&empty;scene_reset();
 target.data=&pixels;target.last_frame=vgl_framecount-10;
 freed=deferred=false;gpu_free_texture_data(&target);
 assert(freed && !deferred); // Old completed texture remains reclaimable.
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'target.cpp'
    exe = Path(tmp) / 'target'
    cpp.write_text(preamble + mark_macro + mark + release + checks)
    subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-fsanitize=address,undefined',
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: GPU destination lifetime prevents immediate texture free; completed textures remain reclaimable')
