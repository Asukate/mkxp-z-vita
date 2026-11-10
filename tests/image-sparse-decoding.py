#!/usr/bin/env python3
"""Run the real PNG decoder with the full RGBA allocation denied."""
from pathlib import Path
import struct,subprocess,tempfile,zlib,sys
root=Path(__file__).resolve().parent.parent
shim=root/'deps/vendor/vita-sdl2-shim'
decoder=Path(sys.argv[1]) if len(sys.argv)>1 else shim/'src/SDL_vita_stubs.c'
def chunk(kind,data):
 return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
def png(raw,w=2000,h=2000):
 rows=b''.join(b'\0'+raw[y*w*4:(y+1)*w*4] for y in range(h))
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
code=r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "display/image-crop.h"
extern "C" {
#include "SDL_surface.h"
#include "SDL_image.h"
#include "SDL_rwops.h"
}
static char error[256];
static size_t denied,fail_size=16000000,extra_fail_size,fail_row_at,row_allocs,max_read;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_calloc(size_t,size_t);
static bool reject(size_t n){
 if((fail_size&&n==fail_size)||(extra_fail_size&&n==extra_fail_size)){++denied;return true;}
 if(n==8000&&fail_row_at&&++row_allocs==fail_row_at)return true;
 return false;
}
extern "C" void *__wrap_malloc(size_t n){return reject(n)?nullptr:__real_malloc(n);}
extern "C" void *__wrap_calloc(size_t n,size_t s){return reject(n*s)?nullptr:__real_calloc(n,s);}
extern "C" int SDL_SetError(const char *fmt,...){va_list a;va_start(a,fmt);vsnprintf(error,sizeof(error),fmt,a);va_end(a);return -1;}
extern "C" const char *SDL_GetError(){return error;}
extern "C" Sint64 SDL_RWsize(SDL_RWops *s){return s->size(s);}
extern "C" Sint64 SDL_RWseek(SDL_RWops *s,Sint64 n,int w){return s->seek(s,n,w);}
extern "C" size_t SDL_RWread(SDL_RWops *s,void *p,size_t n,size_t c){max_read=std::max(max_read,n*c);return s->read(s,p,n,c);}
extern "C" int SDL_RWclose(SDL_RWops *s){return s->close(s);}
static std::vector<unsigned char> input;
static size_t position,available;
static unsigned closes,rewinds;
static bool fail_seek;
static Sint64 size_fn(SDL_RWops*){return input.size();}
static Sint64 seek_fn(SDL_RWops*,Sint64 n,int w){
 if(w==RW_SEEK_CUR&&n==0)return position;
 if(w!=RW_SEEK_SET||n<0||size_t(n)>input.size())return -1;
 if(n==0&&++rewinds>2&&fail_seek)return -1;
 position=n;return n;
}
static size_t read_fn(SDL_RWops*,void *p,size_t n,size_t count){
 size_t left=position<available?available-position:0,want=std::min(n*count,left);
 memcpy(p,input.data()+position,want);position+=want;return n?want/n:0;
}
static int close_fn(SDL_RWops*){++closes;return 0;}
static SDL_RWops stream(){SDL_RWops s={};s.size=size_fn;s.seek=seek_fn;s.read=read_fn;s.close=close_fn;return s;}
static void reset(){position=0;available=input.size();closes=rewinds=0;denied=row_allocs=0;extra_fail_size=fail_row_at=max_read=0;fail_seek=false;error[0]=0;fail_size=16000000;}
static void load(const char *path){FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);input.resize(ftell(f));rewind(f);assert(fread(input.data(),1,input.size(),f)==input.size());fclose(f);}
int main(int argc,char **argv){
 assert(argc==6);load(argv[1]);
 for(int repeat=0;repeat<3;repeat++){
  reset();SDL_RWops s=stream();SDL_Surface *image=IMG_LoadTyped_RW(&s,1,"VITA_BITMAP");
  if(!image){fprintf(stderr,"sparse decode: %s\n",SDL_GetError());return 1;}
  SDL_VitaImageCrop decoded;assert(SDL_VitaGetImageCrop(image,&decoded));
  assert(decoded.x==96&&decoded.y==196&&decoded.width==59&&decoded.height==39);
  assert(decoded.logicalWidth==2000&&decoded.logicalHeight==2000&&decoded.background==0x00ffffff);
  ImageCrop crop;crop.x=decoded.x;crop.y=decoded.y;crop.width=decoded.width;crop.height=decoded.height;
  crop.logicalWidth=decoded.logicalWidth;crop.logicalHeight=decoded.logicalHeight;crop.background=decoded.background;
  for(int y=0;y<2000;y++)for(int x=0;x<2000;x++){
   unsigned expected=0x00ffffff;
   if(x>=100&&x<140&&y>=200&&y<220)expected=0xff21170b;
   if(x==150&&y==230)expected=0x00563412;
   assert(imageCropPixel(crop,image->pixels,image->pitch,x,y)==expected);
  }
  SDL_FreeSurface(image);assert(denied==1&&closes==1&&max_read<=32768);
 }
 // Ordinary SDL clients never receive a cropped logical image.
 reset();SDL_RWops s=stream();assert(!IMG_LoadTyped_RW(&s,1,"PNG")&&closes==1);
 // Successful full allocations retain the original surface contract.
 reset();fail_size=0;s=stream();SDL_Surface *full=IMG_LoadTyped_RW(&s,1,"VITA_BITMAP");
 SDL_VitaImageCrop unused;assert(full&&full->w==2000&&full->h==2000&&!SDL_VitaGetImageCrop(full,&unused));SDL_FreeSurface(full);
 // Either row-allocation failure point (crop scan, crop copy) falls back to bands.
 for(unsigned which=1;which<=2;which++){reset();fail_row_at=which;s=stream();SDL_Surface *b=IMG_LoadTyped_RW(&s,1,"VITA_BITMAP");
  assert(b&&closes==1&&SDL_VitaGetImageBands(b));SDL_FreeSurface(b);}
 // A crop that cannot be allocated either falls back to bands of small allocations.
 reset();extra_fail_size=59*39*4;s=stream();SDL_Surface *banded=IMG_LoadTyped_RW(&s,1,"VITA_BITMAP");
 assert(banded&&closes==1&&SDL_VitaGetImageBands(banded)&&!SDL_VitaGetImageCrop(banded,&unused));
 {const SDL_VitaImageBands *b=SDL_VitaGetImageBands(banded);assert(b->count==1&&b->band[0].ownTop==128&&b->band[0].x==96&&b->band[0].width==59);}
 SDL_FreeSurface(banded);
 // A truncated scan and a rejected rewind preserve stream ownership.
 reset();available=input.size()/2;s=stream();assert(!IMG_LoadTyped_RW(&s,1,"VITA_BITMAP")&&closes==1);
 reset();fail_seek=true;s=stream();assert(!IMG_LoadTyped_RW(&s,0,"VITA_BITMAP")&&closes==0);
 // Opaque/dense images are never an inexact crop; they come back as full-width bands.
 for(int arg=2;arg<=3;arg++){load(argv[arg]);reset();s=stream();SDL_Surface *b=IMG_LoadTyped_RW(&s,1,"VITA_BITMAP");
  assert(b&&closes==1&&!SDL_VitaGetImageCrop(b,&unused));const SDL_VitaImageBands *info=SDL_VitaGetImageBands(b);
  assert(info&&info->count==16&&info->band[0].x==0&&info->band[0].width==2000);SDL_FreeSurface(b);}
 // A constant transparent image needs only one stored texel.
 load(argv[4]);reset();s=stream();SDL_Surface *constant=IMG_LoadTyped_RW(&s,1,"VITA_BITMAP");
 assert(constant&&constant->w==1&&constant->h==1&&SDL_VitaGetImageCrop(constant,&unused));
 assert(unused.background==0x00ffffff);SDL_FreeSurface(constant);
 // A modest crop may fit even when the usual 25 percent saving is unavailable.
 load(argv[5]);reset();s=stream();SDL_Surface *modest=IMG_LoadTyped_RW(&s,1,"VITA_BITMAP");
 assert(modest&&SDL_VitaGetImageCrop(modest,&unused));
 assert(unused.x==46&&unused.y==0&&unused.width==1908&&unused.height==2000);
 auto *row=static_cast<const uint32_t *>(modest->pixels);
 assert(row[3]==0x00ffffff&&row[4]==0xff21170b&&row[1904]==0x00ffffff);
 SDL_FreeSurface(modest);
}
'''
with tempfile.TemporaryDirectory(prefix='hardrpg-sparse-png-') as name:
 p=Path(name);raw=bytearray(b'\xff\xff\xff\0'*(2000*2000))
 for y in range(200,220):
  for x in range(100,140):raw[(y*2000+x)*4:(y*2000+x)*4+4]=bytes((11,23,33,255))
 raw[(230*2000+150)*4:(230*2000+150)*4+4]=bytes((18,52,86,0))
 (p/'sparse.png').write_bytes(png(raw))
 (p/'opaque.png').write_bytes(png(bytes((11,23,33,255))*4000000))
 dense=bytearray(b'\xff\xff\xff\0'*4000000);dense[4:]=bytes((11,23,33,255))*3999999
 (p/'dense.png').write_bytes(png(dense))
 (p/'constant.png').write_bytes(png(b'\xff\xff\xff\0'*4000000))
 modest_row=b'\xff\xff\xff\0'*50+bytes((11,23,33,255))*1900+b'\xff\xff\xff\0'*50
 (p/'modest.png').write_bytes(png(modest_row*2000))
 (p/'check.cpp').write_text(code)
 flags=subprocess.check_output(['pkg-config','--cflags','--libs','freetype2','libpng'],text=True).split()
 common=['-g','-O1','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-I'+str(shim/'include/SDL2'),'-I'+str(shim/'include'),'-I'+str(shim/'src'),'-I'+str(root/'src')]
 subprocess.run(['cc','-std=c99',*common,'-c',str(decoder),*flags,'-o',str(p/'shim.o')],check=True)
 subprocess.run(['c++','-std=c++17',*common,str(p/'check.cpp'),str(p/'shim.o'),'-Wl,--gc-sections','-Wl,--wrap=malloc','-Wl,--wrap=calloc','-lm',*flags,'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check'),*[str(p/n) for n in ['sparse.png','opaque.png','dense.png','constant.png','modest.png']]],check=True)
print('PASS: actual PNG decoder recovers denied full-image storage with bounded rows, exact RGBA crop metadata, opt-in ownership and failure cleanup')
