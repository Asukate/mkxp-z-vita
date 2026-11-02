#!/usr/bin/env python3
"""JPEG and PNG pictures of one colour must decode to the same surface bytes.

The Vita SDL shim decodes PNG through libpng and JPEG through stb_image into
ABGR8888 surfaces (R,G,B,A bytes in memory). A JPEG once came out with red and
blue swapped (issue 11: red caps turned blue). Requires a host C compiler plus
pkg-config, libpng and FreeType development files.
"""
from pathlib import Path
import subprocess, tempfile, struct, zlib

source = Path(__file__).resolve().parent.parent
temporary = tempfile.TemporaryDirectory(prefix='hardrpg-jpeg-colors-')
root = Path(temporary.name)

# 16x16 JPEG filled with rgb(200, 60, 20), quality 95, no chroma subsampling.
(root / 'red.jpg').write_bytes(bytes.fromhex('ffd8ffe000104a46494600010100000100010000ffdb0043000201010101010201010102020202020403020202020504040304060506060605060606070908060709070606080b08090a0a0a0a0a06080b0c0b0a0c090a0a0affdb004301020202020202050303050a0706070a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0affc00011080010001003011100021101031101ffc4001500010100000000000000000000000000000007ffc40014100100000000000000000000000000000000ffc400160101010100000000000000000000000000000809ffc40014110100000000000000000000000000000000ffda000c03010002110311003f0081a4f69200000fffd9'))

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
raw = (b'\0' + bytes((200, 60, 20)) * 16) * 16
(root / 'red.png').write_bytes(b'\x89PNG\r\n\x1a\n' +
    chunk(b'IHDR', struct.pack('>IIBBBBB', 16, 16, 8, 2, 0, 0, 0)) +
    chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

(root / 'jpeg-check.c').write_text(r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL_surface.h"
#include "SDL_error.h"
#include "SDL_image.h"
#include "SDL_rwops.h"
void vitaImageFailure(const char *stage, unsigned width, unsigned height, size_t size) { (void)stage; (void)width; (void)height; (void)size; }
static char error[256];
int SDL_SetError(const char *fmt, ...) { va_list a; va_start(a,fmt); vsnprintf(error,sizeof(error),fmt,a); va_end(a); return -1; }
const char *SDL_GetError(void) { return error; }
Sint64 SDL_RWsize(SDL_RWops *s) { return s->size(s); }
Sint64 SDL_RWseek(SDL_RWops *s,Sint64 n,int w) { return s->seek(s,n,w); }
size_t SDL_RWread(SDL_RWops *s,void *p,size_t n,size_t c) { return s->read(s,p,n,c); }
int SDL_RWclose(SDL_RWops *s) { return s->close(s); }
static unsigned char *bytes; static size_t length, position;
static Sint64 size_fn(SDL_RWops *s) { (void)s; return length; }
static Sint64 seek_fn(SDL_RWops *s,Sint64 n,int w) { (void)s; (void)w; position=n; return n; }
static size_t read_fn(SDL_RWops *s,void *p,size_t n,size_t c) { (void)s; size_t want=n*c; if(want>length-position) want=length-position; memcpy(p,bytes+position,want); position+=want; return want/n; }
static int close_fn(SDL_RWops *s) { (void)s; return 0; }
static SDL_Surface *load(const char *path) {
 FILE *f=fopen(path,"rb"); assert(f); fseek(f,0,SEEK_END); length=ftell(f); rewind(f);
 bytes=malloc(length); assert(fread(bytes,1,length,f)==length); fclose(f); position=0;
 SDL_RWops s={0}; s.size=size_fn; s.seek=seek_fn; s.read=read_fn; s.close=close_fn;
 SDL_Surface *out=IMG_LoadTyped_RW(&s,1,"PNG"); free(bytes);
 if(!out) fprintf(stderr,"decode %s failed: %s\n",path,SDL_GetError());
 assert(out && out->w==16 && out->h==16 && out->format->format==SDL_PIXELFORMAT_ABGR8888);
 return out;
}
static int near(int a,int b) { return abs(a-b)<=3; }
int main(int argc,char **argv) {
 assert(argc==3);
 SDL_Surface *png=load(argv[1]), *jpg=load(argv[2]);
 const unsigned char *p=png->pixels, *j=jpg->pixels;
 printf("PNG bytes %d,%d,%d,%d  JPEG bytes %d,%d,%d,%d\n",p[0],p[1],p[2],p[3],j[0],j[1],j[2],j[3]);
 assert(p[0]==200 && p[1]==60 && p[2]==20 && p[3]==255);
 for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
  const unsigned char *q=j+y*jpg->pitch+x*4;
  if(!(near(q[0],200) && near(q[1],60) && near(q[2],20) && q[3]==255)) {
   fprintf(stderr,"JPEG pixel %d,%d is %d,%d,%d,%d; PNG order is 200,60,20,255\n",x,y,q[0],q[1],q[2],q[3]); return 1;
  }
 }
 SDL_FreeSurface(png); SDL_FreeSurface(jpg);
 puts("PASS: JPEG and PNG of one colour decode to the same R,G,B,A surface bytes");
 return 0;
}
''')
flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'freetype2', 'libpng'], text=True).split()
shim = source/'deps/vendor/vita-sdl2-shim'
subprocess.run(['cc','-std=c99','-g','-O1','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-I'+str(shim/'include/SDL2'),'-I'+str(shim/'include'),str(root/'jpeg-check.c'),str(shim/'src/SDL_vita_stubs.c'),'-Wl,--gc-sections','-lm',*flags,'-o',str(root/'jpeg-check')],check=True)
subprocess.run([str(root/'jpeg-check'), str(root/'red.png'), str(root/'red.jpg')],check=True)
