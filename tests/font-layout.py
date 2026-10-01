#!/usr/bin/env python3
"""Compare production Vita text geometry with the host SDL_ttf implementation.

Coverage alone misses clipped outlined text and jumping per-character baselines.
This compares canvas and ink positions before RGSS's outline crop/composition.
"""
import argparse
import importlib.util
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("font_raster", ROOT / "tests/font-raster.py")
raster = importlib.util.module_from_spec(spec)
spec.loader.exec_module(raster)

CHECKS = r'''
#include <dlfcn.h>
struct Box {int x0,y0,x1,y1;bool operator==(const Box &b)const{return x0==b.x0&&y0==b.y0&&x1==b.x1&&y1==b.y1;}};
Box ink(SDL_Surface *s){
 Box b{s->w,s->h,-1,-1};
 for(int y=0;y<s->h;++y)for(int x=0;x<s->w;++x){
  auto pixel=*reinterpret_cast<Uint32*>(static_cast<Uint8*>(s->pixels)+y*s->pitch+x*4);
  if(pixel&s->format->Amask){b.x0=std::min(b.x0,x);b.y0=std::min(b.y0,y);b.x1=std::max(b.x1,x);b.y1=std::max(b.y1,y);}
 }return b;
}
int main(int argc,char **argv){
 if(argc!=2||!vita_ttf_ensure_library())return 2;
 void *lib=dlopen("libSDL2_ttf.so",RTLD_NOW|RTLD_DEEPBIND);if(!lib){puts(dlerror());return 2;}
 auto init=reinterpret_cast<int(*)()>(dlsym(lib,"TTF_Init"));
 auto open=reinterpret_cast<void*(*)(const char*,int)>(dlsym(lib,"TTF_OpenFont"));
 auto close=reinterpret_cast<void(*)(void*)>(dlsym(lib,"TTF_CloseFont"));
 auto outline=reinterpret_cast<void(*)(void*,int)>(dlsym(lib,"TTF_SetFontOutline"));
 auto kerning=reinterpret_cast<void(*)(void*,int)>(dlsym(lib,"TTF_SetFontKerning"));
 auto render=reinterpret_cast<SDL_Surface*(*)(void*,const char*,SDL_Color)>(dlsym(lib,"TTF_RenderUTF8_Blended"));
 auto height=reinterpret_cast<int(*)(void*)>(dlsym(lib,"TTF_FontHeight"));
 auto measure=reinterpret_cast<int(*)(void*,const char*,int*,int*)>(dlsym(lib,"TTF_SizeUTF8"));
 if(!init||!open||!close||!outline||!kerning||!render||!height||!measure||init()!=0)return 2;
 int cases=0,failures=0;
 for(int size:{14,23,24,26,32}){
  // Compare canvas geometry without mixing modern SDL_ttf's HarfBuzz/GPOS
  // kerning with this port's existing FreeType-only kerning implementation.
  TTF_Font f={};f.ptsize=size;f.kerning=0;
  if(FT_New_Face(vita_ttf_library,argv[1],0,&f.face))return 2;
  FT_Set_Pixel_Sizes(f.face,0,size);
  void *reference=open(argv[1],size);if(!reference)return 2;
  kerning(reference,0);
  if(TTF_FontHeight(&f)!=height(reference)){printf("HEIGHT size=%d actual=%d expected=%d\n",size,TTF_FontHeight(&f),height(reference));++failures;}
  for(int grow:{0,2})for(const char *text:{"G","Items","Skills","Status","Sins","Forest of Abandoned","H","g","y","j","."," ","Items "}){
   f.outline=grow;outline(reference,grow);
   int aw,ah,bw,bh;TTF_SizeUTF8(&f,text,&aw,&ah);measure(reference,text,&bw,&bh);
   if(aw!=bw||ah!=bh){printf("MEASURE size=%d outline=%d text=%s actual=%dx%d expected=%dx%d\n",size,grow,text,aw,ah,bw,bh);++failures;}
   auto *actual=vita_ttf_render(&f,text,SDL_Color{255,255,255,255});
   auto *expected=render(reference,text,SDL_Color{255,255,255,255});
   if(!actual||!expected)return 2;
   Box a=ink(actual),b=ink(expected);
   if(actual->w!=expected->w||actual->h!=expected->h||!(a==b)){
    printf("LAYOUT size=%d outline=%d text=%s actual=%dx%d ink=%d,%d..%d,%d expected=%dx%d ink=%d,%d..%d,%d\n",size,grow,text,actual->w,actual->h,a.x0,a.y0,a.x1,a.y1,expected->w,expected->h,b.x0,b.y0,b.x1,b.y1);++failures;
   }++cases;SDL_FreeSurface(actual);SDL_FreeSurface(expected);
  }close(reference);FT_Done_Face(f.face);
 }printf("cases=%d layout_mismatches=%d\n",cases,failures);return failures?1:0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fonts", nargs="*", type=Path)
    parser.add_argument("--source", type=Path, default=ROOT / "src/vita_stubs.cpp")
    args = parser.parse_args()
    source = args.source.read_text()
    backend = source[source.index("struct TTF_Font\n"):source.index("/* =================================================================\n * pixman")]
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2", "freetype2"], text=True))
    with tempfile.TemporaryDirectory(prefix="hardrpg-layout-test-") as name:
        path = Path(name)
        (path / "layout.cpp").write_text(raster.PREAMBLE + backend + CHECKS)
        subprocess.run(["c++", "-std=c++17", "-O2", str(path / "layout.cpp"), *flags, "-ldl", "-o", str(path / "layout")], check=True)
        failed = False
        for font in args.fonts or [ROOT / "launcher/font.ttf"]:
            print(f"Font: {font}", flush=True)
            failed |= subprocess.run([str(path / "layout"), str(font.resolve())]).returncode != 0
        raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
