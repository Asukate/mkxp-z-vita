#!/usr/bin/env python3
"""Control characters in drawn text must not show up as missing-glyph boxes.

Exit Fate (issue 24) ends some message lines with a TAB, and its message window
draws one character per draw_text call. Windows draws nothing for it; the Vita
font rasterizer draws the font's .notdef box. Compile the production rasterizer
(src/vita_stubs.cpp) and Bitmap's fixupString (src/display/bitmap.cpp) with host
SDL/FreeType and compare ink.
"""
from pathlib import Path
import shlex
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
BITMAP = (ROOT / "src/display/bitmap.cpp").read_text()


def function(source, signature):
    start = source.index(signature)
    pos = source.index("{", start)
    depth = 0
    while True:
        depth += (source[pos] == "{") - (source[pos] == "}")
        if depth == 0:
            return source[start:pos + 1] + "\n"
        pos += 1


PREAMBLE = r'''
#include <SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
#include <algorithm>
#include <vector>
#include <list>
#include <new>
#include <string>
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
static size_t ink(TTF_Font *f, const std::string &text) {
 /* Bitmap::drawText skips an empty string and a lone space before rendering. */
 if (text.empty() || text == " ") return 0;
 SDL_Surface *s = vita_ttf_render(f, text.c_str(), SDL_Color{255,255,255,255});
 if (!s) return (size_t)-1;
 size_t n = 0;
 for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++) {
  auto pixel = *reinterpret_cast<Uint32*>(static_cast<Uint8*>(s->pixels) + y*s->pitch + x*4);
  n += (pixel & s->format->Amask) != 0;
 }
 SDL_FreeSurface(s);
 return n;
}

int main(int argc, char **argv) {
 if (argc != 2 || !vita_ttf_ensure_library()) return 2;
 TTF_Font f = {}; f.ptsize = 20; f.kerning = 1;
 if (FT_New_Face(vita_ttf_library, argv[1], 0, &f.face)) return 2;
 FT_Set_Pixel_Sizes(f.face, 0, 20);
 int failures = 0;
 auto check = [&](bool ok, const char *what) {
  printf("%s: %s\n", ok ? "ok" : "FAIL", what); failures += !ok;
 };
 printf("raw renderer ink for a lone TAB: %zu (non-zero is the box the game showed)\n", ink(&f, "\t"));
 check(fixupString("\t") == " ", "TAB becomes a space");
 check(ink(&f, fixupString("\t")) == 0, "a TAB drawn on its own leaves no ink");
 check(ink(&f, fixupString("stop!\t")) == ink(&f, "stop!"), "a trailing TAB adds no ink");
 check(fixupString("a\r\nb") == "a  b", "CR and LF still become spaces");
 check(fixupString("J\xc3\xa5" "dengand") == "J\xc3\xa5" "dengand", "UTF-8 text is untouched");
 vita_ttf_forget_glyphs(&f);
 FT_Done_Face(f.face);
 printf(failures ? "FAILED (%d)\n" : "all passed\n", failures);
 return failures ? 1 : 0;
}
'''


def main():
    stubs = (ROOT / "src/vita_stubs.cpp").read_text()
    backend = stubs[stubs.index("struct TTF_Font\n"):stubs.index("/* =================================================================\n * pixman")]
    fixup = function(BITMAP, "static std::string fixupString(const char *str)")
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2", "freetype2"], text=True, env={k: v for k, v in os.environ.items() if not k.startswith("PKG_CONFIG_")}))
    with tempfile.TemporaryDirectory(prefix="hardrpg-text-control-") as name:
        work = Path(name)
        cpp = work / "check.cpp"
        cpp.write_text(PREAMBLE + backend + fixup + CHECKS)
        exe = work / "check"
        subprocess.run(["c++", "-std=c++17", "-O2", str(cpp), *flags, "-o", str(exe)], check=True)
        return subprocess.run([str(exe), str(ROOT / "assets/liberation.ttf")]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
