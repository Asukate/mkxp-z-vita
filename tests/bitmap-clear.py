#!/usr/bin/env python3
"""Exercise production Bitmap clear/fill call sites with a queued GPU model.

The fake GL boundary delays clears until Finish, like vitaGL. Bitmap's actual
deferred-clear helpers and public methods are compiled directly from source.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
PREAMBLE = r'''
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
#define __vita__ 1
#define GUARD_MEGA
#define GUARD_ANIMATED
struct Vec4 { int x=0,y=0,z=0,w=0; Vec4()=default; explicit Vec4(int a):w(a){} };
struct IntRect {int x,y,w,h; IntRect(int a,int b,int c,int d):x(a),y(b),w(c),h(d){} };
template<class T> struct State {
 T value{}; std::vector<T> stack;
 void pushSet(T next){stack.push_back(value);value=next;}
 void pop(){value=stack.back();stack.pop_back();}
};
struct {State<bool> scissorTest; State<IntRect> scissorBox{IntRect(0,0,8,4)}; State<Vec4> clearColor;} glState;
std::array<std::array<int,32>,3> pixels;
std::vector<std::function<void()>> queue;
int clearCount=0;
namespace FBO {
 using ID=int; ID boundFramebufferID=2;
 void bind(ID id){boundFramebufferID=id;}
 void clear(){
  int target=boundFramebufferID,color=glState.clearColor.value.w;
  bool clipped=glState.scissorTest.value; IntRect r=glState.scissorBox.value;
  ++clearCount;
  queue.push_back([=]{for(int y=0;y<4;++y)for(int x=0;x<8;++x)
   if(!clipped||(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)) pixels[target][y*8+x]=color;});
 }
}
struct {void Finish(){for(auto &op:queue)op();queue.clear();}} gl;
struct FrameProfile {enum {ClearSync,ClearReplaced,ClearFull,ClearNextRead,ClearNextDraw,ClearPartial};struct Scope {explicit Scope(int){}};};
static unsigned vgl_upload_barrier_count=0;
static unsigned long long vgl_upload_barrier_us=0;
static void vitaDiagLog(const char *,const char *,...){}
struct Bitmap;
struct BitmapPrivate {
 bool pendingClear=false; Vec4 pendingColor; struct {bool enabled=false;} animation;
 void *megaSurface=nullptr; struct {int width=8,height=4;} gl;
 Bitmap *selfHires=nullptr;
 void bindFBO(){FBO::bind(1);}
 void clearTaintedArea(){} void onModified(){} void addTaintedArea(const IntRect &){} void substractTaintedArea(const IntRect &){}
 void fillRect(const IntRect &r,const Vec4 &color){
  bindFBO(); glState.scissorTest.pushSet(true);glState.scissorBox.pushSet(r);glState.clearColor.pushSet(color);
  FBO::clear();glState.clearColor.pop();glState.scissorBox.pop();glState.scissorTest.pop();
 }
};
struct Bitmap {
 BitmapPrivate *p; void guardDisposed(){} bool hasHires(){return false;} int width(){return 8;}int height(){return 4;}
 void clear();void fillRect(const IntRect &,const Vec4 &);void clearRect(const IntRect &);
};
'''
CHECKS = r'''
void reset(){for(auto &image:pixels)image.fill(9);queue.clear();clearCount=0;FBO::bind(2);}
void expect(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main() try {
 BitmapPrivate p;Bitmap b{&p};
 // The launcher's real sequence: clear previous About text, paint Games
 // highlight, then sample the sprite. No old pixels may remain elsewhere.
 reset();b.clear();b.fillRect(IntRect(1,1,3,1),Vec4(7));clearRead(&p);gl.Finish();
 for(int y=0;y<4;++y)for(int x=0;x<8;++x)
  expect(pixels[1][y*8+x]==(y==1&&x>=1&&x<4?7:0),"clear -> fillRect retained previous view pixels");
 expect(pixels[2][0]==9,"materialization cleared another framebuffer");
 expect(clearCount==2,"full clear must happen once before partial fill");
 // A partial clear also cannot cancel the preceding full clear.
 reset();b.clear();b.clearRect(IntRect(1,1,3,1));clearRead(&p);gl.Finish();
 expect(std::all_of(pixels[1].begin(),pixels[1].end(),[](int a){return a==0;}),"clear -> clearRect retained previous pixels");
 // Reads materialize unscissored and restore the caller's target/state.
 reset();glState.scissorTest.pushSet(true);glState.scissorBox.pushSet(IntRect(1,1,1,1));
 b.clear();b.clear();clearRead(&p);
 expect(FBO::boundFramebufferID==2&&glState.scissorTest.value,"clear failed to restore render state");
 expect(clearCount==1,"repeated full clears should coalesce");
 expect(std::all_of(pixels[1].begin(),pixels[1].end(),[](int a){return a==0;}),"full clear inherited partial scissor");
 glState.scissorBox.pop();glState.scissorTest.pop();
 // Only an actual full overwrite may discard a pending clear.
 reset();b.clear();clearFullOverwrite(&p);clearRead(&p);gl.Finish();
 expect(clearCount==0&&!p.pendingClear,"full overwrite needlessly materialized clear");
 std::cout<<"PASS: production clear/fillRect/clearRect sequencing, queued GPU ordering, scissor/FBO restoration, coalescing and full overwrite\n";
} catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
'''


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2] + "\n"


def main():
    source = (ROOT / "src/display/bitmap.cpp").read_text()
    signatures = ["static void materializeClear(BitmapPrivate *p)\n{",
                  "static void clearArm(BitmapPrivate *p)\n{",
                  "static void clearRead(BitmapPrivate *p)\n{",
                  "static void clearBeforePartialWrite(BitmapPrivate *p)\n{",
                  "static void clearFullOverwrite(BitmapPrivate *p)\n{",
                  "void Bitmap::clear()\n{",
                  "void Bitmap::fillRect(const IntRect &rect, const Vec4 &color)\n{",
                  "void Bitmap::clearRect(const IntRect &rect)\n{"]
    with tempfile.TemporaryDirectory(prefix="hardrpg-clear-test-") as name:
        path = Path(name)
        cpp = path / "clear.cpp"
        cpp.write_text(PREAMBLE + "\n".join(function(source, sig) for sig in signatures) + CHECKS)
        subprocess.run(["c++", "-std=c++17", "-O2", str(cpp), "-o", str(path / "clear")], check=True)
        subprocess.run([str(path / "clear")], check=True)


if __name__ == "__main__":
    main()
