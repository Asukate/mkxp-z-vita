#!/usr/bin/env python3
"""Check the production Vita font rasterizer against its FreeType glyph coverage.

Compile its real backend with host SDL/FreeType; only diagnostics and timing
are stubbed. This isolates clipping before GPU upload. Supply extra fonts as
positional arguments to exercise display fonts without copying game assets.
"""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
PREAMBLE = r'''
#include <SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#define TTF_STYLE_NORMAL 0
#define TTF_STYLE_BOLD 1
#define TTF_STYLE_ITALIC 2
struct TTF_Font;
extern "C" TTF_Font *TTF_OpenFontRW(SDL_RWops *, int, int);
struct FrameProfile { enum {TtfMeasure,TtfRaster}; struct Scope {explicit Scope(int){}}; };
static void vitaDiagLog(const char *, const char *, ...) {}
static bool vitaDiagTextProfileEnabled() {return false;}
static uint64_t sceKernelGetSystemTimeWide() {return 0;}
'''
CHECKS = r'''

int main(int argc,char **argv) {
 if(argc!=2 || !vita_ttf_ensure_library()) return 2;
 int failures=0,cases=0;
 for(int size: {14,15,17,21,23,25,26,32}) for(int outline: {0,2}) {
  TTF_Font f={}; f.ptsize=size;f.kerning=1;f.outline=outline;
  if(FT_New_Face(vita_ttf_library,argv[1],0,&f.face)) return 2;
  FT_Set_Pixel_Sizes(f.face,0,size);
  for(const char *text: {"H","a","g","y","G",".","j"}) {
   VitaTtfLayout l;vita_ttf_layout(&f,vita_ttf_decode_utf8(text),true,&l);
   auto *s=vita_ttf_render(&f,text,SDL_Color{255,255,255,255});
   if(!s) return 2;
   size_t expected=0,actual=0;
   for(const auto &glyph:l.glyphs) for(auto coverage:glyph.coverage) expected+=coverage!=0;
   for(int y=0;y<s->h;y++) for(int x=0;x<s->w;x++) {
    auto pixel=*reinterpret_cast<Uint32*>(static_cast<Uint8*>(s->pixels)+y*s->pitch+x*4);
    actual+=(pixel&s->format->Amask)!=0;
   }
   if(expected!=actual) {printf("CLIPPED size=%d outline=%d text=%s ink_y=%d..%d surface=%dx%d expected=%zu actual=%zu\n",size,outline,text,l.min_y,l.max_y,s->w,s->h,expected,actual);failures++;}
   cases++;SDL_FreeSurface(s);
  }
  FT_Done_Face(f.face);
 }
 printf("cases=%d clipped=%d\n",cases,failures);return failures?1:0;
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fonts", nargs="*", type=Path)
    args = parser.parse_args()
    source = (ROOT / "src/vita_stubs.cpp").read_text()
    backend = source[source.index("struct TTF_Font\n"):source.index("/* =================================================================\n * pixman")]
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2", "freetype2"], text=True))
    with tempfile.TemporaryDirectory(prefix="hardrpg-font-test-") as name:
        work = Path(name)
        cpp = work / "raster.cpp"
        cpp.write_text(PREAMBLE + backend + CHECKS)
        executable = work / "raster"
        subprocess.run(["c++", "-std=c++17", "-O2", str(cpp), *flags, "-o", str(executable)], check=True)
        for font in args.fonts or [ROOT / "launcher/font.ttf"]:
            print(f"Font: {font}", flush=True)
            subprocess.run([str(executable), str(font.resolve())], check=True)

if __name__ == "__main__":
    main()
