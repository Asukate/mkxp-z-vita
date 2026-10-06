#!/usr/bin/env python3
"""Exercise the actual SDL image decoder and injected allocation/read failures.

Requires a host C compiler plus pkg-config, libpng and FreeType development files.
"""
from pathlib import Path
import subprocess, sys
import tempfile, struct, zlib

source = Path(__file__).resolve().parent.parent
temporary = tempfile.TemporaryDirectory(prefix='hardrpg-image-decoder-')
root = Path(temporary.name)
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
raw = (b'\0' + bytes((40, 20, 10, 255)) * 960) * 1152
(root / 'valid.png').write_bytes(b'\x89PNG\r\n\x1a\n' +
    chunk(b'IHDR', struct.pack('>IIBBBBB', 960, 1152, 8, 6, 0, 0, 0)) +
    chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
(root / 'decoder-check.c').write_text(r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL_surface.h"
#include "SDL_error.h"
#include "SDL_image.h"
#include "SDL_rwops.h"
static char error[256];
static size_t fail_size;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__wrap_malloc(size_t n) { return n==fail_size ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t s) { return n*s==fail_size ? NULL : __real_calloc(n,s); }
int SDL_SetError(const char *fmt, ...) { va_list args; va_start(args,fmt); vsnprintf(error,sizeof(error),fmt,args); va_end(args); return -1; }
const char *SDL_GetError(void) { return error; }
Sint64 SDL_RWsize(SDL_RWops *s) { return s->size(s); }
Sint64 SDL_RWseek(SDL_RWops *s,Sint64 n,int w) { return s->seek(s,n,w); }
size_t SDL_RWread(SDL_RWops *s,void *p,size_t n,size_t count) { return s->read(s,p,n,count); }
int SDL_RWclose(SDL_RWops *s) { return s->close(s); }
static unsigned char *bytes;
static size_t length,position;
static int closes,short_read;
static Sint64 size_fn(SDL_RWops *s) { (void)s; return length; }
static Sint64 seek_fn(SDL_RWops *s,Sint64 n,int w) { (void)s; (void)w; position=n; return n; }
static size_t read_fn(SDL_RWops *s,void *p,size_t n,size_t count) { (void)s; size_t want=n*count; if(want>length-position)want=length-position; if(short_read && want) --want; memcpy(p,bytes+position,want); position+=want; return want/n; }
static int close_fn(SDL_RWops *s) { (void)s; ++closes; return 0; }
static SDL_Surface *decode(void) { SDL_RWops s={0}; s.size=size_fn; s.seek=seek_fn; s.read=read_fn; s.close=close_fn; error[0]=0; position=0; return IMG_LoadTyped_RW(&s,1,"PNG"); }
static void expect_failure(size_t n,const char *reason) { fail_size=n; int before=closes; SDL_Surface *s=decode(); assert(!s); assert(closes==before+1); if(!strstr(SDL_GetError(),reason)) { fprintf(stderr,"Expected '%s', got SDL='%s' IMG='%s'\n",reason,SDL_GetError(),IMG_GetError()); abort(); } assert(!strcmp(SDL_GetError(),IMG_GetError())); fail_size=0; }
int main(int argc,char **argv) {
 assert(argc==2); FILE *f=fopen(argv[1],"rb"); assert(f); fseek(f,0,SEEK_END); length=ftell(f); rewind(f); bytes=malloc(length); assert(fread(bytes,1,length,f)==length); fclose(f);
 for(int i=0;i<20;++i) { SDL_Surface *s=decode(); if(!s) fprintf(stderr,"valid decode failed: %s\n",SDL_GetError()); assert(s && s->w==960 && s->h==1152); SDL_FreeSurface(s); }
 expect_failure(length,"out of memory reading image");
 expect_failure(960*1152*4,"out of memory allocating PNG pixels");
 expect_failure(1152*sizeof(void*),"out of memory allocating PNG rows");
 short_read=1; expect_failure(0,"incomplete image stream read"); short_read=0;
 bytes[0]=0; expect_failure(0,"not a PNG"); free(bytes);
 puts("PASS: actual SDL image decoder loads PNG repeatedly; allocation/read failures report precise SDL errors and close streams once");
}
''')
flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'freetype2', 'libpng'], text=True).split()
shim = source/'deps/vendor/vita-sdl2-shim'
subprocess.run(['cc','-std=c99','-g','-O1','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-I'+str(shim/'include/SDL2'),'-I'+str(shim/'include'),str(root/'decoder-check.c'),str(shim/'src/SDL_vita_stubs.c'),'-Wl,--gc-sections','-Wl,--wrap=malloc','-Wl,--wrap=calloc','-lm',*flags,'-o',str(root/'decoder-check')],check=True)
subprocess.run([str(root/'decoder-check'), str(root/'valid.png')],check=True)
