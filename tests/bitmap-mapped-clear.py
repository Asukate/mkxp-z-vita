#!/usr/bin/env python3
"""Exercise the actual large-image clear without allocating another image."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent.parent
source=(root/'src/display/bitmap.cpp').read_text()
start=source.index('static bool clearMappedImage(')
body=source[start:source.index('\n}',start)+2]
cpp=r'''
#include "display/image-crop.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <algorithm>
struct Vec4{float x=0,y=0,z=0,w=0;};
struct BitmapPrivate{void *cpu=nullptr;ImageCrop imageCrop;struct{int width,height;struct{unsigned gl=1;}tex;}gl;};
static bool completed;
static unsigned fences;
struct{void Finish(){completed=true;++fences;}}gl;
static void *storage;
void *vglGetTexDataPointerByID(unsigned){assert(completed);return storage;}
static bool banded;
static bool bandsCurrent(BitmapPrivate *){return banded;}
'''+body+r'''
int main(){
 std::vector<unsigned> pixels(1024*1024,0xdeadbeef);storage=pixels.data();
 BitmapPrivate p;p.gl.width=1024;p.gl.height=1024;
 assert(clearMappedImage(&p,Vec4()));assert(fences==1);
 assert(std::all_of(pixels.begin(),pixels.end(),[](unsigned v){return v==0;}));
 // Sparse storage clears both its padded physical rows and logical surround.
 std::fill(pixels.begin(),pixels.end(),0xffffffff);completed=false;
 p.gl.width=3;p.gl.height=2;p.imageCrop.width=3;p.imageCrop.height=2;
 p.imageCrop.logicalWidth=2480;p.imageCrop.logicalHeight=3508;p.imageCrop.background=0x00ffffff;
 assert(clearMappedImage(&p,Vec4()));assert(p.imageCrop.background==0&&fences==2);
 assert(std::all_of(pixels.begin(),pixels.begin()+16,[](unsigned v){return v==0;}));
 assert(pixels[16]==0xffffffff);
 banded=true;assert(!clearMappedImage(&p,Vec4()));banded=false; // bands clear through the full texture
 p.imageCrop=ImageCrop();assert(!clearMappedImage(&p,Vec4()));
 p.gl.width=1024;p.gl.height=1024;p.cpu=&p;assert(!clearMappedImage(&p,Vec4()));p.cpu=nullptr;
 Vec4 nonzero;nonzero.w=1;assert(!clearMappedImage(&p,nonzero));
 storage=nullptr;completed=false;assert(!clearMappedImage(&p,Vec4()));
}
'''
with tempfile.TemporaryDirectory(prefix='hardrpg-mapped-clear-') as name:
 p=Path(name);(p/'check.cpp').write_text(cpp)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I',str(root/'src'),str(p/'check.cpp'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
print('PASS: actual mapped clear fences before writing, clears padded storage and sparse surround, no full-image allocation')
