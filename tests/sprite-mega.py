#!/usr/bin/env python3
"""Compile production sprite assignment/preparation against a host bitmap seam.

The geometry, transform and signal implementations are production code. Bitmap
allocation/blitting use a bounded CPU stand-in. Mesa renders the real effect
shader to compare cached crops with a full GPU sheet; this is not a Vita run.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

PREAMBLE = r'''
#include <GL/glew.h>
#include <SDL.h>
#include <cmath>
#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include "etc-internal.h"
#include "transform.h"
#include "exception.h"
#include "sigslot/signal.hpp"
struct Rect {
 int x=0,y=0,width=0,height=0; sigslot::signal_st<> valueChanged;
 Rect& operator=(const IntRect &r){set(r.x,r.y,r.w,r.h);return *this;}
 void set(int a,int b,int c,int d){x=a;y=b;width=c;height=d;valueChanged();}
 FloatRect toFloatRect() const{return FloatRect(x,y,width,height);}
};
struct Color {Vec4 norm;}; struct Tone {Vec4 norm;};
struct EtcTemps {Rect rect;Color color;Tone tone;};
enum BlendType {BlendNormal};
struct Scene {struct Geometry {IntRect rect{0,0,128,128};};};
struct SVertex {Vec2 pos,tex;};
struct Quad {
 FloatRect tex,pos;
 void setTexRect(const FloatRect &r){tex=r;}
 void setPosRect(const FloatRect &r){pos=r;}
 static void setTexPosRect(SVertex *v,const FloatRect &t,const FloatRect &p){
  v[0]={Vec2(p.x,p.y),Vec2(t.x,t.y)};
  v[1]={Vec2(p.x+p.w,p.y),Vec2(t.x+t.w,t.y)};
  v[2]={Vec2(p.x+p.w,p.y+p.h),Vec2(t.x+t.w,t.y+t.h)};
  v[3]={Vec2(p.x,p.y+p.h),Vec2(t.x,t.y+t.h)};
 }
};
struct SimpleQuadArray {
 int quadCount=0;std::vector<SVertex> vertices;
 void resize(int n){quadCount=n;vertices.resize(n*4);}
 void commit(){}
};
struct {sigslot::signal_st<> prepareDraw;} shared;
auto *shState=&shared;
static float fwrap(float v,float r){float f=fmod(v,r);return f<0?f+r:f;}
static void vitaDiagLog(const char *,const char *,...){}
static void vitaDiagLogVglMemory(const char *){}
struct Bitmap {
 int w,h;bool mega,internal,disposed=false;GLuint tex=0;
 std::vector<unsigned char> pixels;
 sigslot::signal_st<> wasDisposed,modified;
 static int live,allocations,blits,limit;static bool failAllocation,failBlit;
 Bitmap(int x,int y,bool region=true):w(x),h(y),mega(!region),internal(region){
  if(region&&failAllocation){failAllocation=false;throw Exception(Exception::MKXPError,"injected allocation failure");}
  pixels.resize(x*y*4);if(internal){++live;++allocations;}
 }
 ~Bitmap(){if(tex)glDeleteTextures(1,&tex);if(internal)--live;}
 int width()const{return w;} int height()const{return h;}
 bool isDisposed()const{return disposed;}bool isMega()const{return mega;}
 bool hasHires()const{return false;}Bitmap *getHires()const{return nullptr;}
 IntRect rect()const{return IntRect(0,0,w,h);} static int maxSize(){return limit;}
 void ensureNonMega(){if(mega)throw Exception(Exception::MKXPError,"Operation not supported for mega surfaces");}
 void blt(int,int,const Bitmap &src,const IntRect &r){
  if(failBlit){failBlit=false;throw Exception(Exception::MKXPError,"injected blit failure");}
  ++blits;
  for(int y=0;y<r.h;++y)memcpy(&pixels[y*w*4],&src.pixels[((r.y+y)*src.w+r.x)*4],r.w*4);
 }
 void upload(){
  if(!tex)glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 }
 void dispose(){disposed=true;wasDisposed();}
};
int Bitmap::live=0,Bitmap::allocations=0,Bitmap::blits=0,Bitmap::limit=4096;
bool Bitmap::failAllocation=false,Bitmap::failBlit=false;
template<class T> bool nullOrDisposed(T *p){return !p||p->isDisposed();}
'''

SPRITE = r'''
struct Sprite {
 SpritePrivate *p=new SpritePrivate;
 ~Sprite(){delete p;}
 void guardDisposed(){}void setBitmap(Bitmap *);
};
'''

ASSIGNMENT = r'''
int main() {
 try {
  Bitmap sheet(384,4224,false);Sprite popup;
  popup.setBitmap(&sheet);
  popup.p->srcRect->set(360,4200,24,24);
  popup.p->prepare();
  std::cout<<"PASS assignment followed by a 24x24 crop\n";
 } catch(const Exception &e){std::cerr<<e.msg.c_str()<<'\n';return 1;}
}
'''

CHECKS = r'''
void expect(bool b,const char *m){if(!b)throw std::runtime_error(m);}
bool same(float a,float b){return std::abs(a-b)<0.0001f;}
void seed(Bitmap &b){for(int y=0;y<b.h;++y)for(int x=0;x<b.w;++x){
 int n=(y*b.w+x)*4;b.pixels[n]=x%251;b.pixels[n+1]=y%251;
 b.pixels[n+2]=(x*7+y*3)%251;b.pixels[n+3]=((x+y)%7)?255:96;
}}
void select(Sprite &s,int x,int y,int w=24,int h=24){s.p->srcRect->set(x,y,w,h);s.p->prepare();}
GLuint compile(GLenum type,const std::string &s){
 GLuint shader=glCreateShader(type);const char *p=s.c_str();glShaderSource(shader,1,&p,nullptr);glCompileShader(shader);
 GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){char b[8192];glGetShaderInfoLog(shader,sizeof b,nullptr,b);throw std::runtime_error(b);}return shader;
}
std::string read(const std::string &name){std::ifstream f(name);std::stringstream b;b<<f.rdbuf();return b.str();}
GLuint program(const std::string &root){
 std::string prefix="#version 120\n#define lowp\n#define mediump\n#define highp\n";
 GLuint v=compile(GL_VERTEX_SHADER,prefix+read(root+"/shader/sprite.vert"));
 GLuint f=compile(GL_FRAGMENT_SHADER,prefix+read(root+"/shader/sprite.frag"));
 GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
 GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char b[8192];glGetProgramInfoLog(p,sizeof b,nullptr,b);throw std::runtime_error(b);}
 glDeleteShader(v);glDeleteShader(f);return p;
}
void u1(GLuint p,const char *n,float v){glUniform1f(glGetUniformLocation(p,n),v);}
void ui(GLuint p,const char *n,int v){glUniform1i(glGetUniformLocation(p,n),v);}
void u2(GLuint p,const char *n,float x,float y){glUniform2f(glGetUniformLocation(p,n),x,y);}
struct Effect {int pattern=0;bool smooth=false;float opacity=1;};
std::vector<unsigned char> render(Sprite &s,GLuint shader,Bitmap &pattern,Effect effect={}){
 auto &p=*s.p;Bitmap *b=p.megaBitmap?p.megaBitmap.get():p.bitmap;
 b->upload();glActiveTexture(GL_TEXTURE1);pattern.upload();glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,b->tex);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,effect.smooth?GL_LINEAR:GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,effect.smooth?GL_LINEAR:GL_NEAREST);
 glUseProgram(shader);ui(shader,"texture",0);ui(shader,"pattern",1);
 u2(shader,"texSizeInv",1.f/b->w,1.f/b->h);
 u2(shader,"sourceOffset",p.megaBitmap?p.megaRect.x:0,p.megaBitmap?p.megaRect.y:0);
 u2(shader,"sourceSizeInv",1.f/p.bitmap->w,1.f/p.bitmap->h);
 ui(shader,"renderPattern",effect.pattern!=0);ui(shader,"patternTile",effect.pattern==1);
 ui(shader,"patternBlendType",0);u1(shader,"patternOpacity",.4f);
 u2(shader,"patternSizeInv",1.f/pattern.w,1.f/pattern.h);
 u2(shader,"patternScroll",.13f,.24f);u2(shader,"patternZoom",1.1f,.9f);
 u1(shader,"opacity",effect.opacity);
 glUniform4f(glGetUniformLocation(shader,"tone"),p.tone->norm.x,p.tone->norm.y,p.tone->norm.z,p.tone->norm.w);
 glUniform4f(glGetUniformLocation(shader,"color"),p.color->norm.x,p.color->norm.y,p.color->norm.z,p.color->norm.w);
 ui(shader,"bushY",p.bushY);ui(shader,"bushUnder",p.bushUnder);
 u1(shader,"bushSlope",p.bushSlope);u1(shader,"bushIntercept",p.bushIntercept);
 u1(shader,"bushOpacity",p.bushOpacity.norm);ui(shader,"invert",p.invert);
 float proj[16]={1.f/64,0,0,0,0,-1.f/64,0,0,0,0,1,0,-1,1,0,1};
 glUniformMatrix4fv(glGetUniformLocation(shader,"projMat"),1,GL_FALSE,proj);
 glUniformMatrix4fv(glGetUniformLocation(shader,"spriteMat"),1,GL_FALSE,p.trans.getMatrix());
 std::vector<SVertex> vertices;
 if(p.wave.active)vertices=p.wave.qArray.vertices;
 else {vertices.resize(4);Quad::setTexPosRect(vertices.data(),p.quad.tex,p.quad.pos);}
 glClearColor(.1f,.2f,.3f,1);glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 glEnable(GL_SCISSOR_TEST);glScissor(9,7,111,111);
 int position=glGetAttribLocation(shader,"position"),tex=glGetAttribLocation(shader,"texCoord");
 glEnableVertexAttribArray(position);glEnableVertexAttribArray(tex);
 for(size_t i=0;i<vertices.size();i+=4){
  glVertexAttribPointer(position,2,GL_FLOAT,GL_FALSE,sizeof(SVertex),&vertices[i].pos);
  glVertexAttribPointer(tex,2,GL_FLOAT,GL_FALSE,sizeof(SVertex),&vertices[i].tex);
  glDrawArrays(GL_TRIANGLE_FAN,0,4);
 }
 glDisableVertexAttribArray(position);glDisableVertexAttribArray(tex);glDisable(GL_SCISSOR_TEST);
 std::vector<unsigned char> result(128*128*4);glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
 expect(glGetError()==GL_NO_ERROR,"host OpenGL error");return result;
}
void compare(const std::vector<unsigned char> &a,const std::vector<unsigned char> &b,const char *name){
 int worst=0,changed=0;for(size_t i=0;i<a.size();++i){int delta=std::abs((int)a[i]-b[i]);worst=std::max(worst,delta);if(delta>2)++changed;}
 if(changed)throw std::runtime_error(std::string(name)+": differing channels="+std::to_string(changed)+", max="+std::to_string(worst));
}
int main(int argc,char **argv) try {
 SDL_Init(SDL_INIT_VIDEO);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
 SDL_Window *window=SDL_CreateWindow("sprite regression",0,0,128,128,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
 expect(window,"SDL window creation failed");SDL_GLContext context=SDL_GL_CreateContext(window);expect(context,"GL context failed");
 glewInit();glGetError();glViewport(0,0,128,128);
 GLuint candidate=program(argv[1]),baseline=program(argv[2]);
 {
 Bitmap sheet(384,4224,false);seed(sheet);Bitmap oracle(384,4224,false);oracle.mega=false;oracle.pixels=sheet.pixels;
 Bitmap pattern(16,16);seed(pattern);Sprite crop,full;
 int before=Bitmap::allocations;crop.setBitmap(&sheet);full.setBitmap(&oracle);
 expect(Bitmap::allocations==before,"assignment uploaded the full mega sheet before src_rect selection");
 expect(crop.p->bitmap==&sheet&&crop.p->srcRect->height==4224,"assignment changed public bitmap identity/dimensions");
 for(int y:{0,4080,4092,4104,4200}){
  select(crop,360,y);select(full,360,y);
  expect(crop.p->megaBitmap->w<=30&&crop.p->megaBitmap->h<=30,"crop allocation is not bounded");
  int local=((y-crop.p->megaRect.y)*crop.p->megaBitmap->w+360-crop.p->megaRect.x)*4;
  expect(!memcmp(&crop.p->megaBitmap->pixels[local],&sheet.pixels[(y*384+360)*4],4),"wrong icon pixels copied");
  compare(render(crop,candidate,pattern),render(full,baseline,pattern),"icon row");
 }
 select(crop,120,4104);select(full,120,4104);
 before=Bitmap::allocations;int blits=Bitmap::blits;for(int i=0;i<100;++i)crop.p->prepare();
 expect(Bitmap::allocations==before&&Bitmap::blits==blits,"unchanged crop uploaded each frame");
 for(int scenario=0;scenario<18;++scenario){
  for(Sprite *s:{&crop,&full}){
   auto &p=*s->p;p.trans.setPosition(Vec2(49,57));p.trans.setOrigin(Vec2(12,24));
   p.trans.setScale(Vec2(1.71f,1.29f));p.trans.setRotation(scenario>=9?31:0);
   p.mirrored=scenario%2;p.onSrcRectChange();p.bushDepth=scenario%3==0?9:0;p.bushDirty=true;
   p.tone->norm=Vec4(.06f,-.03f,.02f,.17f);p.color->norm=Vec4(.8f,.1f,.2f,.12f);
   p.invert=scenario%5==0;p.wave.amp=scenario>=12?4:0;p.wave.dirty=true;p.prepare();
  }
  Effect effect;effect.pattern=scenario%3;effect.smooth=scenario%2;effect.opacity=.73f;
  compare(render(crop,candidate,pattern,effect),render(full,baseline,pattern,effect),"effects, origin, zoom, rotation, mirror, wave, clipping");
  compare(render(full,candidate,pattern,effect),render(full,baseline,pattern,effect),"ordinary sprite shader regression");
 }
 // Signal invalidation, per-sprite ownership, and failure-safe cache replacement.
 Sprite shared;shared.setBitmap(&sheet);select(shared,0,4104);
 before=Bitmap::allocations;sheet.pixels[(4104*384+120)*4]=99;sheet.modified();crop.p->prepare();shared.p->prepare();
 expect(Bitmap::allocations==before+2,"source modification did not invalidate both sprites");
 auto *old=crop.p->megaBitmap.get();crop.p->srcRect->set(144,4104,24,24);Bitmap::failAllocation=true;
 try{crop.p->prepare();expect(false,"allocation failure swallowed");}catch(const Exception &){}
 expect(crop.p->megaBitmap.get()==old,"allocation failure destroyed previous valid cache");crop.p->prepare();
 old=crop.p->megaBitmap.get();crop.p->srcRect->set(168,4104,24,24);int live=Bitmap::live;Bitmap::failBlit=true;
 try{crop.p->prepare();expect(false,"blit failure swallowed");}catch(const Exception &){}
 expect(crop.p->megaBitmap.get()==old&&Bitmap::live==live,"failed blit leaked or published partial cache");crop.p->prepare();
 // Invisible and empty crops never allocate, including entirely out-of-bounds rectangles.
 Sprite hidden;hidden.setBitmap(&sheet);hidden.p->opacity=0;before=Bitmap::allocations;hidden.p->prepare();
 expect(Bitmap::allocations==before,"invisible full sheet allocated");hidden.p->opacity=255;select(hidden,0,9000);
 expect(!hidden.p->isVisible&&!hidden.p->megaBitmap,"out-of-bounds crop allocated");
 select(hidden,-5,-7);expect(hidden.p->megaRect.x==0&&hidden.p->megaRect.y==0,"negative source crop escaped image bounds");
 crop.setBitmap(nullptr);expect(!crop.p->bitmap&&!crop.p->megaBitmap,"nil assignment retained cache");
 expect(shared.p->megaBitmap!=nullptr,"disposing one sprite destroyed another's crop");sheet.dispose();
 expect(!shared.p->bitmap&&!shared.p->megaBitmap&&!hidden.p->megaBitmap,"source disposal retained sprite caches");
 Sprite normal;normal.setBitmap(&pattern);before=Bitmap::allocations;normal.p->prepare();
 expect(!normal.p->megaBitmap&&Bitmap::allocations==before,"ordinary sprite entered crop path");
 // An uncropped image larger than the GPU limit still needs tiled rendering.
 Bitmap big(384,4224,false);Sprite oversized;oversized.setBitmap(&big);
 try{oversized.p->prepare();expect(false,"oversized visible selection was accepted");}catch(const Exception &e){expect(e.msg.find("source rectangle")!=std::string::npos,"missing bounded-selection diagnostic");}
 }
 expect(Bitmap::live==0,"internal bitmap ownership leak");
 glDeleteProgram(candidate);glDeleteProgram(baseline);SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
 std::cout<<"PASS production Sprite assignment/prepare: exact icon sequence, boundary rows, crop cache, shared mutation/disposal, nil, empty/clipped regions, allocation/blit rollback\n";
 std::cout<<"PASS Mesa shader pixels: 18 effect combinations against baseline, mirror/origin/zoom/rotation/wave/scissor, smooth/pattern/bush/tone/alpha; ordinary sprites unchanged\n";
} catch(const Exception &e){std::cerr<<e.msg.c_str()<<'\n';return 1;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
'''


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT)
    parser.add_argument("--baseline", type=Path, default=ROOT)
    parser.add_argument("--assignment-only", action="store_true")
    args = parser.parse_args()
    source = (args.source / "src/display/sprite.cpp").read_text()
    private = source[source.index("struct SpritePrivate\n{"):source.index("\n};", source.index("struct SpritePrivate\n{")) + 3]
    cpp = PREAMBLE + private + SPRITE + function(source, "void Sprite::setBitmap(Bitmap *bitmap)\n{")
    cpp += ASSIGNMENT if args.assignment_only else CHECKS
    with tempfile.TemporaryDirectory(prefix="hardrpg-sprite-mega-") as name:
        path = Path(name)
        (path / "sprite.cpp").write_text(cpp)
        flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2", "glew"], text=True).split()
        includes = [f"-I{args.source / 'src' / part}" for part in ("etc", "util", "display/gl")]
        subprocess.run(["c++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        *includes, str(path / "sprite.cpp"), *flags, "-o", str(path / "sprite")], check=True)
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1", LIBGL_ALWAYS_SOFTWARE="1")
        command = [str(path / "sprite"), str(args.source.resolve()), str(args.baseline.resolve())]
        if not args.assignment_only:
            command = ["xvfb-run", "-a", *command]
        subprocess.run(command, check=True, env=env)


if __name__ == "__main__":
    main()
