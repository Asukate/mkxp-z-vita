#!/usr/bin/env python3
"""Banded PNG decode: when neither the full image nor its crop can be allocated,
the bitmap decoder returns 128-row bands of small allocations. Checks every
pixel of the bands against the source, for a sparse transparent image and an
opaque one, under ASan/UBSan.

Requires a host C compiler plus pkg-config, libpng and FreeType development files.
"""
from pathlib import Path
import random, struct, subprocess, tempfile, zlib

source = Path(__file__).resolve().parent.parent
temporary = tempfile.TemporaryDirectory(prefix='hardrpg-image-bands-')
root = Path(temporary.name)

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

def write_png(path, width, height, pixels):
    raw = b''.join(b'\0' + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' +
                     chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 1)) + chunk(b'IEND', b''))
    (path.with_suffix('.rgba')).write_bytes(pixels)

W, H = 2048, 1500
rng = random.Random(7)
# Sparse: transparent background (with nonzero RGB under alpha 0), scattered blobs
# spread over the whole picture so its crop is nearly full size.
sparse = bytearray(bytes((9, 8, 7, 0)) * W * H)
blobs = [(10, 0, 60, 40), (1980, 30, 2040, 90), (500, 127, 520, 129), (900, 600, 1400, 700),
         (5, 1450, 40, 1500), (2000, 1490, 2048, 1500)]
for x0, y0, x1, y1 in blobs:
    for y in range(y0, y1):
        for x in range(x0, x1):
            o = (y * W + x) * 4
            sparse[o:o + 4] = bytes((rng.randrange(256), rng.randrange(256), rng.randrange(256), 1 + rng.randrange(255)))
write_png(root / 'sparse.png', W, H, bytes(sparse))
opaque = bytearray(W * H * 4)
for y in range(H):
    row = bytes(v for x in range(W) for v in ((x * 7 + y) & 255, (y * 3) & 255, (x ^ y) & 255, 255))
    opaque[y * W * 4:(y + 1) * W * 4] = row
write_png(root / 'opaque.png', W, H, bytes(opaque))

(root / 'bands-check.c').write_text(r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL_surface.h"
#include "SDL_error.h"
#include "SDL_image.h"
#include "SDL_rwops.h"
void vitaImageFailure(const char *stage, unsigned width, unsigned height, size_t size) { (void)stage; (void)width; (void)height; (void)size; }
static char error[256];
static size_t fail_min = (size_t)-1;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__wrap_malloc(size_t n) { return n>=fail_min ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t s) { return n*s>=fail_min ? NULL : __real_calloc(n,s); }
int SDL_SetError(const char *fmt, ...) { va_list args; va_start(args,fmt); vsnprintf(error,sizeof(error),fmt,args); va_end(args); return -1; }
const char *SDL_GetError(void) { return error; }
Sint64 SDL_RWsize(SDL_RWops *s) { return s->size(s); }
Sint64 SDL_RWseek(SDL_RWops *s,Sint64 n,int w) { return s->seek(s,n,w); }
size_t SDL_RWread(SDL_RWops *s,void *p,size_t n,size_t count) { return s->read(s,p,n,count); }
int SDL_RWclose(SDL_RWops *s) { return s->close(s); }
static unsigned char *bytes; static size_t length, position;
static Sint64 size_fn(SDL_RWops *s) { (void)s; return length; }
static Sint64 seek_fn(SDL_RWops *s,Sint64 n,int w) { (void)s; (void)w; position=n; return n; }
static size_t read_fn(SDL_RWops *s,void *p,size_t n,size_t count) { (void)s; size_t want=n*count; if(want>length-position)want=length-position; memcpy(p,bytes+position,want); position+=want; return want/n; }
static int close_fn(SDL_RWops *s) { (void)s; return 0; }
static unsigned char *load(const char *path, size_t *n) { FILE *f=fopen(path,"rb"); assert(f); fseek(f,0,SEEK_END); *n=ftell(f); rewind(f); unsigned char *b=malloc(*n); assert(fread(b,1,*n,f)==*n); fclose(f); return b; }
static SDL_Surface *decode(void) { SDL_RWops s={0}; s.size=size_fn; s.seek=seek_fn; s.read=read_fn; s.close=close_fn; position=0; return IMG_LoadTyped_RW(&s,1,"VITA_BITMAP"); }
static void check(const char *png, const char *rgba, int opaque) {
 size_t rn; bytes=load(png,&length); unsigned char *ref=load(rgba,&rn);
 const int W=2048, H=1500; assert(rn==(size_t)W*H*4);
 fail_min = 4u*1024*1024; /* full (12 MB) and any near-full crop fail; bands fit */
 SDL_Surface *s=decode(); fail_min=(size_t)-1;
 if(!s) { fprintf(stderr,"banded decode failed: %s\n",SDL_GetError()); abort(); }
 SDL_VitaImageCrop crop; assert(!SDL_VitaGetImageCrop(s,&crop));
 const SDL_VitaImageBands *b=SDL_VitaGetImageBands(s); assert(b);
 assert(b->logicalWidth==W && b->logicalHeight==H);
 uint32_t bg; memcpy(&bg,ref,4); assert(b->background==bg);
 int prevBottom=0, owned=0, trimmed=0;
 for(int i=0;i<b->count;++i) {
  const SDL_VitaImageBand *d=&b->band[i];
  assert(d->pixels && d->width>0 && d->height>0);
  assert(d->ownTop>=prevBottom && d->ownBottom>d->ownTop && d->ownBottom-d->ownTop<=128);
  assert(d->y==(d->ownTop?d->ownTop-1:0) && d->y+d->height==(d->ownBottom<H?d->ownBottom+1:H));
  if(d->width<W) ++trimmed;
  if(opaque) assert(d->x==0 && d->width==W);
  prevBottom=d->ownBottom; owned+=d->ownBottom-d->ownTop;
  for(int y=d->y;y<d->y+d->height;++y)
   assert(!memcmp((unsigned char*)d->pixels+(size_t)(y-d->y)*d->width*4, ref+((size_t)y*W+d->x)*4, (size_t)d->width*4));
 }
 /* Every pixel outside the bands' stored columns, and every row of no band, is background. */
 for(int y=0;y<H;++y) {
  const SDL_VitaImageBand *own=NULL;
  for(int i=0;i<b->count;++i) if(y>=b->band[i].ownTop && y<b->band[i].ownBottom) own=&b->band[i];
  for(int x=0;x<W;++x) {
   uint32_t v; memcpy(&v,ref+((size_t)y*W+x)*4,4);
   if(!own || x<own->x || x>=own->x+own->width) assert(v==bg);
  }
 }
 if(opaque) assert(owned==H && b->count==(H+127)/128);
 else assert(trimmed>0 && b->count<(H+127)/128);
 SDL_VitaReleaseImageBand(s,0); assert(!SDL_VitaGetImageBands(s)->band[0].pixels);
 printf("%s: %d bands, %d trimmed\n", opaque?"opaque":"sparse", b->count, trimmed);
 SDL_FreeSurface(s); free(ref); free(bytes);
}
int main(int argc,char **argv) {
 assert(argc==5);
 check(argv[1],argv[2],0);
 check(argv[3],argv[4],1);
 /* Plenty of memory: an ordinary full surface, no bands. */
 bytes=load(argv[1],&length); SDL_Surface *s=decode(); assert(s && s->w==2048 && !SDL_VitaGetImageBands(s)); SDL_FreeSurface(s); free(bytes);
 puts("PASS: banded decode keeps every pixel, trims sparse bands, frees cleanly");
}
''')
flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'freetype2', 'libpng'], text=True).split()
shim = source/'deps/vendor/vita-sdl2-shim'
subprocess.run(['cc','-std=c99','-g','-O1','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-I'+str(shim/'include/SDL2'),'-I'+str(shim/'include'),str(root/'bands-check.c'),str(shim/'src/SDL_vita_stubs.c'),'-Wl,--gc-sections','-Wl,--wrap=malloc','-Wl,--wrap=calloc','-lm',*flags,'-o',str(root/'bands-check')],check=True)
subprocess.run([str(root/'bands-check'), str(root/'sparse.png'), str(root/'sparse.rgba'), str(root/'opaque.png'), str(root/'opaque.rgba')],check=True)
