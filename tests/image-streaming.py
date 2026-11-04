#!/usr/bin/env python3
"""Run the bitmap file-read path and real SDL decoder with a large PNG.

Reject whole-file allocations while allowing the decoded image's pixels. This
models the reported allocation boundary without bundling proprietary game data.
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

source = Path(__file__).resolve().parent.parent
bitmap = (source / 'src/display/bitmap.cpp').read_text()
start = bitmap.index('static SDL_Surface *readImageStream(')
end = bitmap.index('\nstruct BitmapOpenHandler', start)
helper = bitmap[start:end]
shim = source / 'deps/vendor/vita-sdl2-shim'

def chunk(kind, data):
    return (struct.pack('>I', len(data)) + kind + data +
            struct.pack('>I', zlib.crc32(kind + data)))

with tempfile.TemporaryDirectory(prefix='hardrpg-image-streaming-') as temporary:
    root = Path(temporary)
    raw = (b'\0' + bytes((40, 20, 10, 255)) * 2000) * 2000
    png = (b'\x89PNG\r\n\x1a\n' +
           chunk(b'IHDR', struct.pack('>IIBBBBB', 2000, 2000, 8, 6, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(raw, level=0)) + chunk(b'IEND', b''))
    (root / 'large.png').write_bytes(png)
    code = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
extern "C" {
#include "SDL_surface.h"
#include "SDL_error.h"
#include "SDL_image.h"
#include "SDL_rwops.h"
}
static char error[256];
static size_t fail_size, denied, max_read;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_calloc(size_t,size_t);
extern "C" void *__wrap_malloc(size_t n) {
    if (fail_size && n == fail_size) { ++denied; return nullptr; }
    return __real_malloc(n);
}
extern "C" void *__wrap_calloc(size_t n,size_t s) {
    if (fail_size && n * s == fail_size) { ++denied; return nullptr; }
    return __real_calloc(n,s);
}
extern "C" int SDL_SetError(const char *fmt, ...) {
    va_list args; va_start(args,fmt); vsnprintf(error,sizeof(error),fmt,args);
    va_end(args); return -1;
}
extern "C" const char *SDL_GetError(void) { return error; }
extern "C" Sint64 SDL_RWsize(SDL_RWops *s) { return s->size(s); }
extern "C" Sint64 SDL_RWseek(SDL_RWops *s,Sint64 n,int w) { return s->seek(s,n,w); }
extern "C" size_t SDL_RWread(SDL_RWops *s,void *p,size_t n,size_t count) {
    max_read = std::max(max_read, n * count); return s->read(s,p,n,count);
}
extern "C" int SDL_RWclose(SDL_RWops *s) { return s->close(s); }
static std::vector<unsigned char> input;
static size_t position, available;
static int closes;
static SDL_RWops *memory_stream;
static Sint64 size_fn(SDL_RWops *) { return input.size(); }
static Sint64 seek_fn(SDL_RWops *,Sint64 n,int w) {
    if (w != RW_SEEK_SET || n < 0 || size_t(n) > input.size()) return -1;
    position = size_t(n); return n;
}
static size_t read_fn(SDL_RWops *,void *p,size_t n,size_t count) {
    size_t want = n * count;
    size_t left = position < available ? available - position : 0;
    want = std::min(want,left); memcpy(p,input.data()+position,want);
    position += want; return n ? want / n : 0;
}
static int close_fn(SDL_RWops *s) {
    ++closes;
    if (s == memory_stream) { delete memory_stream; memory_stream = nullptr; }
    return 0;
}
static SDL_RWops stream() {
    SDL_RWops s = {}; s.size=size_fn; s.seek=seek_fn; s.read=read_fn; s.close=close_fn;
    return s;
}
extern "C" SDL_RWops *SDL_RWFromConstMem(const void *,int) {
    memory_stream = new SDL_RWops(stream()); position=0; return memory_stream;
}
'''
    code += helper
    code += r'''
static void reset(size_t limit) {
    position=0; available=limit; closes=0; error[0]=0; max_read=0; denied=0;
}
int main(int argc,char **argv) {
    assert(argc==2); FILE *f=fopen(argv[1],"rb"); assert(f);
    fseek(f,0,SEEK_END); input.resize(size_t(ftell(f))); rewind(f);
    assert(fread(input.data(),1,input.size(),f)==input.size()); fclose(f);
    assert(input.size()>16000000);
    for (int i=0;i<3;++i) {
        reset(input.size()); fail_size=input.size(); SDL_RWops s=stream();
        SDL_Surface *image=readImageStream(s,"PNG");
        if (!image) { fprintf(stderr,"Large PNG failed: %s\n",SDL_GetError()); return 1; }
        assert(image->w==2000 && image->h==2000);
        const unsigned char expected[]={40,20,10,255};
        assert(!memcmp(image->pixels,expected,4));
        assert(!memcmp((unsigned char *)image->pixels+1999*image->pitch+1999*4,expected,4));
        SDL_FreeSurface(image);
        assert(denied==0 && closes==1 && max_read<=32768);
    }
    fail_size=0;
    reset(128); SDL_RWops truncated=stream();
    assert(!IMG_LoadTyped_RW(&truncated,1,"PNG") && closes==1);
    assert(strstr(SDL_GetError(),"incomplete image stream read"));
    reset(input.size()); SDL_RWops retained=stream();
    SDL_Surface *image=IMG_LoadTyped_RW(&retained,0,"PNG");
    assert(image && closes==0); SDL_FreeSurface(image);
    reset(128); SDL_RWops retained_failure=stream();
    assert(!IMG_LoadTyped_RW(&retained_failure,0,"PNG") && closes==0);
    puts("PASS: bitmap reader + real PNG decoder handle 16 MB input without full-file copies; pixels, bounded reads, short-read cleanup and stream ownership verified");
}
'''
    (root / 'check.cpp').write_text(code)
    flags = subprocess.check_output(
        ['pkg-config', '--cflags', '--libs', 'freetype2', 'libpng'], text=True).split()
    common = ['-g', '-O1', '-ffunction-sections', '-fdata-sections',
              '-fsanitize=address,undefined', '-I'+str(shim/'include/SDL2'),
              '-I'+str(shim/'include')]
    subprocess.run(['cc', '-std=c99', *common, '-c', str(shim/'src/SDL_vita_stubs.c'),
                    *flags, '-o', str(root/'shim.o')], check=True)
    subprocess.run(['c++', '-std=c++17', '-D__vita__', *common, str(root/'check.cpp'),
                    str(root/'shim.o'), '-Wl,--gc-sections', '-Wl,--wrap=malloc',
                    '-Wl,--wrap=calloc', '-lm', *flags, '-o', str(root/'check')], check=True)
    subprocess.run([str(root/'check'), str(root/'large.png')], check=True)
