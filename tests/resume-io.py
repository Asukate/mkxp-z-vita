#!/usr/bin/env python3
"""Fault-inject the production SDL output and PhysicsFS read callbacks."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

def function(source, name):
    start = source.index(name)
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

audio = (ROOT / 'deps/vendor/vita-sdl2-shim/src/SDL_vita.c').read_text()
audio = audio[audio.index('static unsigned audio_resume_pending;'):audio.index('Uint32 SDL_GetQueuedAudioSize')]
audio_prefix = r'''
#include <cassert>
#include <cstring>
#include <cstdint>
using Uint8 = uint8_t; using Uint32 = uint32_t; using SDL_AudioDeviceID = unsigned;
struct State { int audio_paused, audio_initialized, audio_port, audio_samples, audio_channels; };
State sdl_state{0, 1, 42, 512, 2};
const int SCE_AUDIO_OUT_PORT_TYPE_BGM=1, SCE_AUDIO_OUT_MODE_STEREO=2, SCE_AUDIO_OUT_MODE_MONO=1;
static int opens, releases, outputs; static bool failOpen, stale;
static short expected[1024];
int sceAudioOutOpenPort(int, int samples, int rate, int mode) {
    ++opens; assert(samples == 512 && rate == 48000 && mode == 2); return failOpen ? -1 : 99;
}
int sceAudioOutReleasePort(int) { ++releases; return 0; }
int sceAudioOutOutput(int, const void *pcm) {
    ++outputs; assert(!memcmp(pcm, expected, sizeof(expected)));
    if (stale) { stale=false; return -1; } return 0;
}
int SDL_SetError(const char *, ...) { return -1; }
'''
audio_test = r'''
int main() {
    for (int i=0; i<1024; ++i) expected[i]=i;
    assert(SDL_QueueAudio(1, expected, sizeof(expected))==0 && opens==0 && outputs==1);
    SDL_VitaRequestAudioResume();
    assert(SDL_QueueAudio(1, expected, sizeof(expected))==0 && opens==1 && releases==1);
    stale=true;
    assert(SDL_QueueAudio(1, expected, sizeof(expected))==0 && opens==2 && outputs==4);
    failOpen=true; SDL_VitaRequestAudioResume();
    assert(SDL_QueueAudio(1, expected, sizeof(expected))<0 && opens==3);
    failOpen=false;
    assert(SDL_QueueAudio(1, expected, sizeof(expected))==0 && opens==4);
    sdl_state.audio_paused=1; SDL_VitaRequestAudioResume();
    assert(SDL_QueueAudio(1, expected, sizeof(expected))==0 && opens==4);
    sdl_state.audio_paused=0;
    assert(SDL_QueueAudio(1, expected, sizeof(expected))==0 && opens==5);
    sdl_state.audio_initialized=0;
    assert(SDL_QueueAudio(1, expected, sizeof(expected))==0 && opens==5);
}
'''

fs = (ROOT / 'src/filesystem/filesystem.cpp').read_text()
callbacks = fs[fs.index('static inline PHYSFS_File *sdlPHYS'):fs.index('/* Copies the first srcN')]
callbacks += function(fs, 'static void initReadOps(')
fs_prefix = r'''
#include <cassert>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <limits>
#include <new>
#include "src/filesystem/resume-read.h"
using Uint32=uint32_t; using Sint64=int64_t; using PHYSFS_sint64=int64_t;
const int RW_SEEK_SET=0, RW_SEEK_CUR=1, RW_SEEK_END=2;
const unsigned SDL_RWOPS_PHYSFS=10;
struct PHYSFS_File { int64_t pos=0; bool stale=false, closed=false; };
struct SDL_RWops {
 Sint64 (*size)(SDL_RWops*); Sint64 (*seek)(SDL_RWops*, int64_t, int);
 size_t (*read)(SDL_RWops*, void*, size_t, size_t);
 size_t (*write)(SDL_RWops*, const void*, size_t, size_t); int (*close)(SDL_RWops*);
 unsigned type; struct { struct { void *data1,*data2; } unknown; } hidden;
};
static PHYSFS_File handles[64]; static int opens; static unsigned generation; static bool failOpen;
static const char content[]="abcdefghijklmnop";
unsigned vitaResumeEpoch() { return generation; }
PHYSFS_File *PHYSFS_openRead(const char*) { return failOpen ? nullptr : &handles[opens++]; }
int PHYSFS_close(PHYSFS_File *f) { f->closed=true; return 1; }
Sint64 PHYSFS_fileLength(PHYSFS_File *f) { return f->stale ? -1 : 16; }
Sint64 PHYSFS_tell(PHYSFS_File *f) { return f->stale ? -1 : f->pos; }
int PHYSFS_seek(PHYSFS_File *f, int64_t p) { if(f->stale||p<0||p>16)return 0;f->pos=p;return 1; }
Sint64 PHYSFS_readBytes(PHYSFS_File *f, void *p, size_t n) {
 if(f->stale)return -1;
 n=std::min<size_t>(n,16-f->pos);memcpy(p,content+f->pos,n);f->pos+=n;return n;
}
Sint64 PHYSFS_writeBytes(PHYSFS_File*,const void*,size_t) { return -1; }
void SDL_FreeRW(SDL_RWops*) {}
'''
fs_test = r'''
int main() {
 SDL_RWops rw{}; auto first=PHYSFS_openRead("Audio/BGM.ogg"); initReadOps(first,rw,false,"Audio/BGM.ogg");
 char bytes[16]{};
 assert(rw.read(&rw,bytes,0,1)==0 && rw.read(&rw,bytes,2,SIZE_MAX)==0);
 assert(rw.read(&rw,bytes,1,4)==4 && !memcmp(bytes,"abcd",4));
 first->stale=true; ++generation;
 assert(rw.read(&rw,bytes,1,4)==4 && !memcmp(bytes,"efgh",4) && first->closed);
 assert(rw.seek(&rw,-2,RW_SEEK_CUR)==6);
 assert(rw.seek(&rw,-2,RW_SEEK_END)==14);
 assert(rw.read(&rw,bytes,1,4)==2 && !memcmp(bytes,"op",2));
 int count=opens; assert(rw.read(&rw,bytes,1,4)==0 && opens==count);
 assert(rw.seek(&rw,INT64_MAX,RW_SEEK_CUR)==-1 && opens==count);
 assert(rw.seek(&rw,-1,RW_SEEK_SET)==-1);
 ++generation; failOpen=true;
 assert(rw.size(&rw)==-1 && rw.read(&rw,bytes,1,1)==0 && !sdlPHYS(&rw)->closed);
 failOpen=false;
 assert(rw.size(&rw)==16 && opens==count+1);
 assert(rw.seek(&rw,0,RW_SEEK_SET)==0);
 sdlPHYS(&rw)->stale=true; // lost handle without a delivered resume event
 assert(rw.read(&rw,bytes,1,4)==4 && !memcmp(bytes,"abcd",4));
 assert(rw.close(&rw)==0 && rw.hidden.unknown.data1==nullptr && rw.hidden.unknown.data2==nullptr);
}
'''
with tempfile.TemporaryDirectory() as directory:
    for name, code, defines in [('audio', audio_prefix+audio+audio_test, []),
                                ('filesystem', fs_prefix+callbacks+fs_test, ['-D__vita__'])]:
        cpp, exe = Path(directory)/f'{name}.cpp', Path(directory)/name
        cpp.write_text(code)
        subprocess.run(['c++','-std=c++14','-Wall','-Wextra',*defines,'-I',str(ROOT),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
print('PASS: production audio-port repair, PCM retry, paused output; read/seek/EOF/overflow/resume/failure/close callbacks')
