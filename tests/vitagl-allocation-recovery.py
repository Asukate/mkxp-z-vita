#!/usr/bin/env python3
"""Exercise the real allocator recovery and Finish before forced reclamation."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1])

def function(path, signature):
    text = path.read_text()
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

finish = function(source / 'source/gxm.c', 'void glFinish(void)')
recover = function(source / 'source/utils/gpu_utils.c', 'void *gpu_alloc_mapped_aligned_unsafe(')
preamble = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#define GL_TRUE true
#define GL_FALSE false
#define FRAME_PURGE_FREQ 4
enum vglMemType { VGL_MEM_VRAM,VGL_MEM_RAM,VGL_MEM_SLOW,VGL_MEM_BUDGET,VGL_MEM_EXTERNAL };
static int unsafe_allocator_counter,gxm_context,gc_mutex[2],allocated;
static unsigned allocation_recovery_count,vgl_finish_epoch;
static bool needs_end_scene=true,needs_scene_reset,dirty_framebuffer;
static bool vgl_gpu_write_pending=true,use_extra_mem=true,submitted,complete;
static bool reclaimed_while_owned;
void mkxp_gpu_allocation_recovery(){++allocation_recovery_count;}
void scene_end(){submitted=true;}
void sceGxmFinish(int){complete=submitted;}
void sceKernelWaitSema(int,int,void*){}
void sceKernelSignalSema(int,int){reclaimed_while_owned=needs_end_scene||!complete;}
void sceKernelDelayThread(int){}
void *vgl_memalign(size_t,size_t,vglMemType){return &allocated;}
'''
checks = r'''
int main(){
 auto *p=gpu_alloc_mapped_aligned_unsafe(16,4096,VGL_MEM_VRAM);
 if(reclaimed_while_owned)return 1;
 assert(p==&allocated && complete && !needs_end_scene);
 assert(needs_scene_reset && dirty_framebuffer && !vgl_gpu_write_pending);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp, exe = Path(tmp)/'recovery.cpp', Path(tmp)/'recovery'
    cpp.write_text(preamble + finish + recover + checks)
    subprocess.run(['c++','-std=c++14','-Wall','-Wextra',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: allocator submits and completes an open scene before forced collection')
