#!/usr/bin/env python3
"""Font name lists must reach the engine fallback pack, and MS Gothic must keep
its half-em Latin grid.

Exit Fate (issue 22) asks for ["MS Gothic", "ＭＳ ゴシック", "med ＭＳゴシック",
"Courier New"] and places intro letters on a fixed grid; its dialogue box fits
10 px per letter at size 20. Compile the production name resolution
(src/display/font.cpp) and the production Vita rasterizer (src/vita_stubs.cpp)
with host SDL/FreeType and check both the chosen family and the glyph advances.
"""
from pathlib import Path
import shlex
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
FONT = (ROOT / "src/display/font.cpp").read_text()


def block(source, signature, tail=""):
    start = source.index(signature)
    pos = source.index("{", start)
    depth = 0
    while True:
        depth += (source[pos] == "{") - (source[pos] == "}")
        if depth == 0:
            return source[start:pos + 1] + tail + "\n"
        pos += 1


PREAMBLE = r'''
#include <SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define TTF_STYLE_NORMAL 0
#define TTF_STYLE_BOLD 1
#define TTF_STYLE_ITALIC 2
struct TTF_Font;
extern "C" TTF_Font *TTF_OpenFontRW(SDL_RWops *, int, int);
struct FrameProfile { enum {TtfMeasure,TtfRaster}; struct Scope {explicit Scope(int){}}; };
static void vitaDiagLog(const char *, const char *, ...) {}
static bool vitaDiagTextProfileEnabled() {return false;}
static uint64_t sceKernelGetSystemTimeWide() {return 0;}
struct Debug { template <class T> Debug &operator<<(const T &) { return *this; } };
'''

STATE = r'''
struct Subs {
 std::map<std::string, std::string> m;
 bool contains(const std::string &k) const { return m.count(k) != 0; }
 std::string operator[](const std::string &k) const { return m.at(k); }
};
struct SharedFontStatePrivate {
 Subs subs; std::map<std::string, FontSet> sets;
 struct UserFont { std::string path; long index = 0; bool regular = false; };
 std::unordered_map<std::string, UserFont> userFonts;
};
typedef SharedFontStatePrivate Priv;
struct SharedFontState {
 Priv *p;
 bool fontPresent(std::string family, bool withFallbackPack = true) const;
};
'''

CHECKS = r'''
static int failures = 0;
static void check(bool ok, const char *what) { printf("%s: %s\n", ok ? "ok" : "FAIL", what); failures += !ok; }
static std::string lower(std::string s) { for (auto &c : s) c = std::tolower((unsigned char)c); return s; }
static std::string pick(const SharedFontState &s, std::vector<std::string> names, bool pack = true) {
 std::string out; pickExistingFontName(names, out, s, pack); return out;
}

int main(int argc, char **argv) {
 if (argc != 2 || !vita_ttf_ensure_library()) return 2;
 Priv priv; SharedFontState sfs{&priv};
 const std::vector<std::string> exitFate = {"MS Gothic", "\xef\xbc\xad\xef\xbc\xb3 \xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf",
                                            "med \xef\xbc\xad\xef\xbc\xb3\xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf", "Courier New"};

 /* Name resolution */
 check(pick(sfs, exitFate) == "MS Gothic", "Exit Fate's list keeps MS Gothic");
 check(std::string(builtinFallbackFor(lower("MS Gothic")) ?: "") == "kosugi", "MS Gothic is drawn with Kosugi");
 check(std::string(builtinFallbackFor(lower(exitFate[1])) ?: "") == "kosugi", "full-width \xef\xbc\xad\xef\xbc\xb3 \xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf is drawn with Kosugi");
 check(pick(sfs, {"Courier New"}) == "Courier New", "font.ini's Courier New is kept (Liberation Mono)");
 check(pick(sfs, {"Georgia", "Times New Roman"}) == "Georgia", "a list picks its first pack family");
 check(pick(sfs, {"No Such Font", "Another"}) == "", "unknown names still fall back to the default face");
 check(pick(sfs, {"UmePlus Gothic", "MS Gothic", "Courier New"}, false) == "", "engine defaults (RGSS2) do not switch to the pack");
 check(pick(sfs, {"Arial"}, false) == "", "engine defaults (RGSS1) are unchanged");
 priv.sets["ms gothic"].regular = "Fonts/msgothic.ttc";
 check(sfs.fontPresent("MS Gothic"), "a game's own MS Gothic is present (and wins in getFont)");
 priv.sets.erase("ms gothic");
 priv.subs.m["ms gothic"] = "my font";
 check(!sfs.fontPresent("MS Gothic"), "a per-game fontSub to a missing font is honoured, not the pack");
 priv.subs.m.clear();

 /* Rasterizer: half-em monospace Latin, like MS Gothic */
 for (int ppem : {19, 20, 21, 24}) {
  TTF_Font f = {}; f.kerning = 1;
  if (FT_New_Face(vita_ttf_library, argv[1], 0, &f.face) || vita_ttf_set_size(&f, ppem)) return 2;
  int lo = 1 << 30, hi = 0;
  for (char c = 0x21; c < 0x7f; c++) { char s[2] = {c, 0}; int w = 0; TTF_SizeUTF8(&f, s, &w, nullptr); lo = std::min(lo, w); hi = std::max(hi, w); }
  char what[96];
  snprintf(what, sizeof what, "ppem %d: every ASCII letter is %d..%d px (want %d..%d)", ppem, lo, hi, ppem / 2, (ppem + 1) / 2);
  check(lo == hi && lo >= ppem / 2 && hi <= (ppem + 1) / 2, what);
  if (ppem == 20) {
   int w = 0; TTF_SizeUTF8(&f, "You may have heard rumours about what has happened", &w, nullptr);
   snprintf(what, sizeof what, "a 50-letter Exit Fate line is %d px (box 514)", w);
   check(w <= 514, what);
  }
  vita_ttf_forget_glyphs(&f); FT_Done_Face(f.face);
 }
 printf(failures ? "FAILED (%d)\n" : "all passed\n", failures);
 return failures ? 1 : 0;
}
'''


def main():
    stubs = (ROOT / "src/vita_stubs.cpp").read_text()
    backend = stubs[stubs.index("struct TTF_Font\n"):stubs.index("/* =================================================================\n * pixman")]
    parts = [
        block(FONT, "struct FontSet\n", ";"),
        STATE,
        block(FONT, "static const char *builtinFallbackFor("),
        block(FONT, "static bool bundledFallbackPackAvailable()"),
        block(FONT, "bool SharedFontState::fontPresent("),
        block(FONT, "static bool pickExistingFontName("),
    ]
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2", "freetype2"], text=True, env={k: v for k, v in os.environ.items() if not k.startswith("PKG_CONFIG_")}))
    with tempfile.TemporaryDirectory(prefix="hardrpg-font-fallback-") as name:
        cpp = Path(name, "check.cpp")
        cpp.write_text(PREAMBLE + backend + "".join(parts) + CHECKS)
        exe = Path(name, "check")
        subprocess.run(["c++", "-std=gnu++17", "-O2", "-D__vita__", str(cpp), *flags, "-o", str(exe)], check=True)
        return subprocess.run([str(exe), str(ROOT / "assets/kosugi.ttf")]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
