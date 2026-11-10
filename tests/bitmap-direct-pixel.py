#!/usr/bin/env python3
"""Exercise the actual Vita large-bitmap single-pixel path without a readback allocation."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent.parent
source=(root/'src/display/bitmap.cpp').read_text()
start=source.index('#ifdef __vita__\n    if (!p->cpu && (p->imageCrop.active()')
body=source[start:source.index('\n#endif',start)]+'\n#endif\n'
cpp=r'''
#include "display/image-crop.h"
#include <cassert>
#include <vector>
#include <stdexcept>
struct Color { unsigned r,g,b,a; Color(unsigned r,unsigned g,unsigned b,unsigned a):r(r),g(g),b(b),a(a){} };
struct Exception : std::runtime_error { enum {MKXPError};Exception(int,const char *m):std::runtime_error(m){} };
static int fences;
struct {void Finish(){++fences;}} gl;
static const void *storage;
static const void *bandStorage[3];
const void *vglGetTexDataPointerByID(unsigned id){return id>=10?bandStorage[id-10]:storage;}
struct ImageBand { struct {struct {unsigned gl;} tex;} tex; ImageCrop crop; int ownTop,ownBottom; };
struct Private {
 void *cpu=nullptr;ImageCrop imageCrop;uint32_t contentVersion=0,directReadVersion=~0u;
 struct {int width,height;struct {unsigned gl=1;} tex;} gl;
 std::vector<ImageBand> bands;
};
static bool bandsCurrent(Private *p){return !p->bands.empty();}
struct Bitmap {
 Private *p;int width()const{return p->imageCrop.active()?p->imageCrop.logicalWidth:p->gl.width;}
 int height()const{return p->imageCrop.active()?p->imageCrop.logicalHeight:p->gl.height;}
 Color read(int x,int y){
'''+body+r'''
 return Color(1,2,3,4); // Existing small-bitmap fallback, not part of this seam.
 }
};
int main(){
 std::vector<uint32_t> pixels(2480*3508,0x7f412111);storage=pixels.data();
 Private p;p.gl.width=2480;p.gl.height=3508;Bitmap b{&p};
 auto c=b.read(2479,3507);assert(c.r==17&&c.g==33&&c.b==65&&c.a==127&&fences==1);
 b.read(0,0);assert(fences==1);++p.contentVersion;b.read(1,1);assert(fences==2);
 pixels[1]=0x0080ff22;c=b.read(1,0);assert(c.r==34&&c.g==255&&c.b==128&&c.a==0);
 p.imageCrop={};p.imageCrop.x=13;p.imageCrop.y=17;p.imageCrop.width=3;p.imageCrop.height=2;
 p.imageCrop.logicalWidth=2480;p.imageCrop.logicalHeight=3508;p.imageCrop.background=0x00ffffff;
 p.gl.width=3;p.gl.height=2;pixels[8+2]=0xff123456;
 c=b.read(15,18);assert(c.r==86&&c.g==52&&c.b==18&&c.a==255);
 c=b.read(0,0);assert(c.r==255&&c.g==255&&c.b==255&&c.a==0);
 // Banded: each row reads its own band; rows of no band and columns outside are background.
 std::vector<uint32_t> band0(4*3,0xff000001),band1(8*4,0xff000002);band1[8*1+1]=0xffabcdef;
 bandStorage[0]=band0.data();bandStorage[1]=band1.data();
 ImageBand a{};a.tex.tex.gl=10;a.crop=p.imageCrop;a.crop.x=100;a.crop.y=0;a.crop.width=4;a.crop.height=3;a.ownTop=0;a.ownBottom=2;
 ImageBand d{};d.tex.tex.gl=11;d.crop=p.imageCrop;d.crop.x=200;d.crop.y=255;d.crop.width=5;d.crop.height=4;d.ownTop=256;d.ownBottom=258;
 p.bands={a,d};p.imageCrop=a.crop;
 c=b.read(101,1);assert(c.r==1&&c.a==255);
 c=b.read(201,256);assert(c.r==0xef&&c.g==0xcd&&c.b==0xab);
 c=b.read(200,257);assert(c.r==2);
 c=b.read(150,1);assert(c.r==255&&c.a==0);
 c=b.read(101,100);assert(c.r==255&&c.g==255&&c.b==255&&c.a==0);
 p.bands.clear();
 storage=nullptr;try{b.read(0,0);assert(false);}catch(const Exception &){}
 puts("PASS: real large-image get_pixel path reads one mapped texel, padded rows and hidden RGB, fences once per content version, no full readback");
}
'''
with tempfile.TemporaryDirectory(prefix='hardrpg-pixel-') as name:
    p=Path(name);(p/'check.cpp').write_text(cpp)
    subprocess.run(['c++','-std=c++17','-D__vita__','-fsanitize=address,undefined','-I',str(root/'src'),str(p/'check.cpp'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True)
