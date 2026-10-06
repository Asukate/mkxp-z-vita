#!/usr/bin/env python3
"""Model GXM color-surface ownership around the exact patched glFinish."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
patch = (root / 'deps/patches/vitaGL/7f16d15c-vita-mkxp.patch').read_text()
hunk = patch.rsplit('--- a/source/gxm.c', 1)[1].split('@@\n', 1)[-1]
lines = hunk.splitlines()
first = next(i for i, line in enumerate(lines) if line == ' void glFinish(void) {')
lines = lines[first:]

def function(old):
    result = []
    for line in lines:
        if line.startswith(' ') or line.startswith('-' if old else '+'):
            result.append(line[1:])
        if line == ' }':
            break
    return '\n'.join(result) + '\n'

preamble = r'''
#include <cassert>
#include <vector>
#define GL_TRUE true
#define GL_FALSE false
static bool dirty_framebuffer,needs_scene_reset,needs_end_scene=true,vgl_gpu_write_pending=true;
static unsigned vgl_finish_epoch;
static int gxm_context,pixel=3,tile=3,begins;
static std::vector<int> stores;
void scene_end() { stores.push_back(tile); }
void sceGxmFinish(int) { for (int v:stores)pixel=v;stores.clear(); }
void scene_reset() {
 if(needs_end_scene)scene_end();
 tile=pixel;needs_end_scene=true;needs_scene_reset=false;dirty_framebuffer=false;++begins;
}
'''
checks = r'''
int main() {
 glFinish();
 pixel=9; // CPU upload after GPU fence, no subsequent GPU draw.
 glFinish();
 if(pixel!=9)return 1; // An empty replacement scene stored old tiles.
 assert(begins==0 && !needs_end_scene && needs_scene_reset && dirty_framebuffer);
 assert(vgl_finish_epoch==2 && !vgl_gpu_write_pending);
 scene_reset(); // Ordinary next draw reopens a scene with current pixels.
 assert(begins==1 && needs_end_scene);
 glFinish();assert(pixel==9 && !needs_end_scene);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    for old in (True, False):
        cpp = path / ('old.cpp' if old else 'fixed.cpp')
        exe = cpp.with_suffix('')
        cpp.write_text(preamble + function(old) + checks)
        subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', str(cpp), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)])
        assert result.returncode == (1 if old else 0)
print('PASS: prior Finish exposes stale-store failure; patched Finish preserves CPU uploads and restarts on next draw')
