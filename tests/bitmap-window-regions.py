#!/usr/bin/env python3
"""Exercise production oversized-window drawing with a texture-size-limited GPU."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SOURCE = (ROOT / 'src/display/bitmap.cpp').read_text()


def function(signature):
    start = SOURCE.index(signature)
    pos = SOURCE.index('{', start)
    depth = 0
    while True:
        depth += (SOURCE[pos] == '{') - (SOURCE[pos] == '}')
        if depth == 0:
            return SOURCE[start:pos + 1] + '\n'
        pos += 1


PREAMBLE = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <stdexcept>
#include <vector>
struct IntRect { int x=0,y=0,w=0,h=0; IntRect()=default; IntRect(int a,int b,int c,int d):x(a),y(b),w(c),h(d){} };
struct FloatRect { float x,y,w,h; FloatRect(float a,float b,float c,float d):x(a),y(b),w(c),h(d){} };
struct Vec2i { int x,y; Vec2i(int a,int b):x(a),y(b){} };
struct Vec4 { float x,y,z,w; Vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
static IntRect normalizedRect(IntRect r) { if(r.w<0){r.x+=r.w;r.w=-r.w;} if(r.h<0){r.y+=r.h;r.h=-r.h;} return r; }
struct SDL_Surface { int w,h,pitch; void *pixels; };
struct Exception {};
static struct { struct { int maxTexSize=4096; } caps; } glState;
static bool reclaimRequested=false, failAllocation=false;
static int uploads=0, allocations=0;
static const int GL_RGBA=0;
namespace TEX {
using ID=unsigned;
struct Texture { int w=0,h=0; std::vector<uint32_t> pixels; };
static std::map<ID,Texture> textures;
static ID bound=0, next=1;
static ID gen() { ID id=next++; textures[id]; return id; }
static void bind(ID id) { bound=id; }
static void del(ID id) { textures.erase(id); }
static void setRepeat(bool) {}
static void setSmooth(bool) {}
static void allocEmpty(int w,int h) {
    assert(w>0 && h>0 && w<=glState.caps.maxTexSize && h<=glState.caps.maxTexSize);
    if(failAllocation){failAllocation=false;throw Exception();}
    ++allocations; auto &t=textures.at(bound); t.w=w;t.h=h;t.pixels.assign((size_t)w*h,0);
}
static void uploadSubImage(int x,int y,int w,int h,const void *pixels,int) {
    auto &t=textures.at(bound); assert(x==0 && y>=0 && w==t.w && y+h<=t.h);
    std::memcpy(t.pixels.data()+(size_t)y*w,pixels,(size_t)w*h*4); ++uploads;
}
}
static IntRect observed;
static std::vector<uint32_t> output;
static float expectedOpacity=1;
struct ShaderBase { Vec2i size{0,0}; void setTexSize(Vec2i s){size=s;} };
struct Quad {
    FloatRect src{0,0,0,0},dst{0,0,0,0}; Vec4 color{0,0,0,0};
    void setTexPosRect(FloatRect s,FloatRect d){src=s;dst=d;}
    void setColor(Vec4 c){color=c;}
    void draw() {
        const auto &t=TEX::textures.at(TEX::bound);
        assert(src.x==0 && src.y==0 && src.w==t.w && src.h==t.h);
        assert(dst.w==t.w && dst.h==t.h && color.w==expectedOpacity);
        for(int y=0;y<t.h;++y) for(int x=0;x<t.w;++x) {
            int ox=(int)dst.x+x-observed.x, oy=(int)dst.y+y-observed.y;
            assert(ox>=0 && oy>=0 && ox<observed.w && oy<observed.h);
            output[(size_t)oy*observed.w+ox]=t.pixels[(size_t)y*t.w+x];
        }
    }
};
struct SharedState { Quad quad; Quad &gpQuad(){return quad;} } state;
static SharedState *shState=&state;
struct BitmapPrivate {
    SDL_Surface *cpu=nullptr;
    TEX::ID windowTex=0; IntRect windowRect;
    uint32_t contentVersion=0,windowVersion=~0u;
};
class Bitmap {
public:
    BitmapPrivate *p;
    explicit Bitmap(BitmapPrivate *v):p(v){}
    void guardDisposed(){}
    bool drawCpuWindow(ShaderBase &,const IntRect &,float);
    static void releaseRecreatableMemory();
    static void reclaimMemory();
};
static std::list<BitmapPrivate *> cpuBitmapList;
static int promotions=0,releases=0;
static void textCacheClear(){++releases;}
static void dropAllReadbacks(){++releases;}
namespace AssetPrefetch { static void releaseMemory(){++releases;} }
static void cpuPromote(BitmapPrivate *p){assert(p->cpu->w<=glState.caps.maxTexSize && p->cpu->h<=glState.caps.maxTexSize);cpuBitmapList.remove(p);p->cpu=nullptr;++promotions;}
'''

CHECKS = r'''
static void render(Bitmap &bitmap,const IntRect &view,bool cached=false) {
    observed=cpuClip(view,bitmap.p->cpu->w,bitmap.p->cpu->h);
    output.assign((size_t)observed.w*observed.h,0);
    expectedOpacity=0.375f;
    ShaderBase shader;
    const int before=uploads;
    assert(bitmap.drawCpuWindow(shader,view,expectedOpacity));
    for(int y=0;y<observed.h;++y) for(int x=0;x<observed.w;++x) {
        const auto *row=(const uint32_t *)((const uint8_t *)bitmap.p->cpu->pixels+(size_t)(observed.y+y)*bitmap.p->cpu->pitch);
        assert(output[(size_t)y*observed.w+x]==row[observed.x+x]);
    }
    if(cached) assert(before==uploads);
}
static void exercise(int w,int h) {
    // Deliberately padded rows make incorrect packed-row assumptions visible.
    const int stride=w+7;
    std::vector<uint32_t> pixels((size_t)stride*h,0xdeadbeef);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x)
        pixels[(size_t)y*stride+x]=(uint32_t)(y*65599u+x) ^ 0x7fc00000u;
    SDL_Surface surface{w,h,stride*4,pixels.data()};
    BitmapPrivate data; data.cpu=&surface; Bitmap bitmap(&data);
    render(bitmap,IntRect(0,0,std::min(w,260),std::min(h,220)));
    render(bitmap,IntRect(0,0,std::min(w,260),std::min(h,220)),true);
    render(bitmap,IntRect(std::max(0,w/2-11),std::max(0,h/2-39),260,220));
    render(bitmap,IntRect(std::max(0,w-51),std::max(0,h-67),260,220));
    render(bitmap,IntRect(-13,-27,80,90));
    render(bitmap,IntRect(w+1,h+1,80,90));
    pixels[0]=0x11223344; ++data.contentVersion;
    render(bitmap,IntRect(0,0,std::min(w,80),std::min(h,90)));
    // A failed allocation leaves CPU contents intact and retries after reclaim.
    TEX::del(data.windowTex); data.windowTex=0;
    failAllocation=true; reclaimRequested=false;
    observed=IntRect(0,0,1,1); output.assign(1,0); ShaderBase shader;
    assert(bitmap.drawCpuWindow(shader,observed,expectedOpacity));
    assert(reclaimRequested && data.windowTex==0 && output[0]==0);
    render(bitmap,observed);
    TEX::del(data.windowTex);
}
int main() {
    exercise(280,4560); exercise(280,7440); exercise(280,24000);
    exercise(32,100000); exercise(100000,32);
    // Tightly packed full-width contents can upload directly from CPU pixels.
    std::vector<uint32_t> packed(280*7440,0xa0123456);
    SDL_Surface packedSurface{280,7440,280*4,packed.data()};
    BitmapPrivate packedData; packedData.cpu=&packedSurface; Bitmap packedBitmap(&packedData);
    render(packedBitmap,IntRect(0,7000,280,200));
    TEX::del(packedData.windowTex);
    // A smaller hardware cap forces both horizontal and vertical tile seams.
    glState.caps.maxTexSize=128;
    // Cache assertions above apply to single tiles; use a separate large view.
    std::vector<uint32_t> pixels(560*7440,0x80aabbcc);
    SDL_Surface surface{560,7440,560*4,pixels.data()};
    BitmapPrivate data; data.cpu=&surface; Bitmap bitmap(&data);
    render(bitmap,IntRect(17,4090,420,500));
    TEX::del(data.windowTex);
    glState.caps.maxTexSize=4096;
    SDL_Surface small{32,32,128,nullptr}; BitmapPrivate ordinary; ordinary.cpu=&small;
    Bitmap regular(&ordinary); ShaderBase shader;
    assert(!regular.drawCpuWindow(shader,IntRect(0,0,32,32),1));
    // An oversized head must not stop reclamation of later ordinary bitmaps.
    SDL_Surface large{280,7440,1120,nullptr}; BitmapPrivate pinned; pinned.cpu=&large;
    pinned.windowTex=TEX::gen(); ordinary.windowTex=TEX::gen();
    cpuBitmapList={&pinned,&ordinary};
    Bitmap::reclaimMemory();
    assert(promotions==1 && releases==3 && cpuBitmapList.size()==1 && pinned.cpu==&large);
    assert(pinned.windowTex==0 && ordinary.windowTex==0);
    puts("PASS: logical extents beyond 7440 and 32767; scrolling, clipping, padded rows, tile seams, alpha, edits, caching and allocation recovery");
}
'''

program = PREAMBLE + function('static bool cpuOversized(') + function('static IntRect cpuClip(')
program += function('bool Bitmap::drawCpuWindow(')
program += function('void Bitmap::releaseRecreatableMemory(') + function('void Bitmap::reclaimMemory(') + CHECKS
with tempfile.TemporaryDirectory(prefix='hardrpg-window-regions-') as directory:
    directory = Path(directory)
    source, binary = directory / 'regions.cpp', directory / 'regions'
    source.write_text(program)
    subprocess.run(['c++', '-std=c++17', '-g', '-O1', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
