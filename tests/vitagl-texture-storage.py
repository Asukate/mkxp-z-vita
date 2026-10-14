#!/usr/bin/env python3
"""Exercise actual vitaGL texture allocation when no pool has memory.

Pass the pinned, patched vitaGL source tree used by the dependency build.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1])
gpu = (source / 'source/utils/gpu_utils.c').read_text()

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

allocate = function(gpu, 'void gpu_alloc_texture(')
preamble = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
typedef unsigned char GLboolean;
typedef int SceGxmTextureFormat;
enum { TEX_UNUSED, TEX_UNINITIALIZED, TEX_VALID };
enum { VGL_MEM_MAIN };
#define OBJ_NOT_USED UINT32_MAX
#define SCE_GXM_TEXTURE_BASE_FORMAT_P8 0x1000000
#define VGL_ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define vgl_memset memset
#define vgl_fast_memcpy memcpy
struct SceGxmTexture { void *data; unsigned width, height; };
struct texture {
 int status; void *data; void *palette_data; uint32_t last_frame; int mip_count;
 SceGxmTexture gxm_tex;
};
static bool poolsEmpty;
static void *released;
static void *color_table;
static uint32_t vgl_texture_alloc_failures;
static uint8_t tex_format_to_bytespp(SceGxmTextureFormat) { return 4; }
static void *gpu_alloc_mapped(size_t size, int) { return poolsEmpty ? nullptr : malloc(size); }
static void gpu_free_texture_data(texture *tex) { released = tex->data; free(tex->data); tex->data = nullptr; }
static void vglInitLinearTexture(SceGxmTexture *t, void *data, SceGxmTextureFormat, unsigned w, unsigned h, int) {
 t->data = data; t->width = w; t->height = h;
}
'''
checks = r'''
int main() {
 // A recycled slot still describes the 32x32 texture that lived there.
 int stale;
 texture slot{TEX_UNINITIALIZED, nullptr, nullptr, 0, 0, {&stale, 32, 32}};
 poolsEmpty = true;
 gpu_alloc_texture(544, 416, 0, nullptr, &slot, 4, nullptr, nullptr, 1);
 assert(vgl_texture_alloc_failures == 1);
 assert(slot.status == TEX_UNINITIALIZED && !slot.data);
 // A framebuffer attach reads this descriptor; it must not name old storage.
 assert(!slot.gxm_tex.data && !slot.gxm_tex.width && !slot.gxm_tex.height);

 // Reallocating a live texture releases its storage before allocating.
 poolsEmpty = false;
 texture live{TEX_UNINITIALIZED, nullptr, nullptr, 0, 0, {nullptr, 0, 0}};
 gpu_alloc_texture(64, 16, 0, nullptr, &live, 4, nullptr, nullptr, 1);
 assert(vgl_texture_alloc_failures == 1 && live.status == TEX_VALID);
 assert(live.data && live.gxm_tex.data == live.data && live.gxm_tex.width == 64);
 void *old = live.data;
 poolsEmpty = true;
 gpu_alloc_texture(64, 16, 0, nullptr, &live, 4, nullptr, nullptr, 1);
 assert(released == old && vgl_texture_alloc_failures == 2);
 assert(live.status == TEX_UNINITIALIZED && !live.data && !live.gxm_tex.data);

 // The slot is reusable once memory returns.
 poolsEmpty = false;
 gpu_alloc_texture(64, 16, 0, nullptr, &live, 4, nullptr, nullptr, 1);
 assert(live.status == TEX_VALID && live.data && vgl_texture_alloc_failures == 2);
 free(live.data);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'storage.cpp'
    exe = Path(tmp) / 'storage'
    cpp.write_text(preamble + allocate + checks)
    subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-fpermissive', '-w',
                    '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: a failed texture allocation is counted and leaves no descriptor of released storage')
