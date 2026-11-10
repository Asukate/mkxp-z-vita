#!/usr/bin/env python3
"""Fonts a player copies into the HardRPG fonts folder reach the games that ask
for them.

Builds a stand-in for Windows' msgothic.ttc (a collection: face 0 "MS Gothic" /
"ＭＳ ゴシック", face 1 "MS PGothic" / "ＭＳ Ｐゴシック") plus a regular and a bold
"UmePlus Gothic" from the repository's own free fonts, runs the production
SharedFontState::initUserFonts / fontPresent / pickExistingFontName
(src/display/font.cpp) over that folder and opens each face with the
production Vita TTF_OpenFontIndexRW (src/vita_stubs.cpp). Needs fontTools,
SDL2 and freetype2.
"""
from pathlib import Path
import importlib.util
import os
import shlex
import subprocess
import tempfile

from fontTools.ttLib import TTFont
from fontTools.ttLib.ttCollection import TTCollection

ROOT = Path(__file__).resolve().parent.parent
FONT = (ROOT / "src/display/font.cpp").read_text()

spec = importlib.util.spec_from_file_location("font_fallback", ROOT / "tests/font-fallback.py")
ff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ff)


def renamed(src, family, local=None, style="Regular"):
    font = TTFont(src)
    name = font["name"]
    for record in list(name.names):
        if record.nameID in (1, 2, 4, 6, 16, 17):
            name.removeNames(nameID=record.nameID)
    name.setName(family, 1, 3, 1, 0x409)
    name.setName(style, 2, 3, 1, 0x409)
    name.setName(f"{family} {style}", 4, 3, 1, 0x409)
    name.setName(family.replace(" ", "") + "-" + style, 6, 3, 1, 0x409)
    if local:
        name.setName(local, 1, 3, 1, 0x411)
    return font


EXTRA = r'''
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include <dirent.h>
#include <iconv.h>
struct Exception {};
struct Encoding {
 static std::string convertString(const std::string &in, const char *from) {
  iconv_t cd = iconv_open("UTF-8", from);
  if (cd == (iconv_t)-1) throw Exception();
  std::string out(in.size() * 4 + 4, '\0');
  char *src = const_cast<char *>(in.data()), *dst = &out[0];
  size_t inLeft = in.size(), outLeft = out.size();
  size_t r = iconv(cd, &src, &inLeft, &dst, &outLeft);
  iconv_close(cd);
  if (r == (size_t)-1) throw Exception();
  out.resize(out.size() - outLeft);
  return out;
 }
};
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
struct SharedFontState {
 SharedFontStatePrivate *p;
 bool fontPresent(std::string family, bool withFallbackPack = true) const;
 int initUserFonts(const std::string &dir);
 static std::string userFontDir();
};
'''

CHECKS = r'''
extern "C" TTF_Font *TTF_OpenFontIndexRW(SDL_RWops *src, int freesrc, int ptsize, long index);
static int failures = 0;
static void check(bool ok, const char *what) { printf("%s: %s\n", ok ? "ok" : "FAIL", what); failures += !ok; }
static const SharedFontStatePrivate::UserFont *user(SharedFontStatePrivate &p, const char *family) {
 auto it = p.userFonts.find(family); return it == p.userFonts.end() ? nullptr : &it->second;
}
static std::string faceFamily(const SharedFontStatePrivate::UserFont *u) {
 if (!u) return "";
 TTF_Font *f = TTF_OpenFontIndexRW(SDL_RWFromFile(u->path.c_str(), "rb"), 1, 0, u->index);
 if (!f) return "<open failed>";
 std::string name = f->face->family_name;
 vita_ttf_forget_glyphs(f); FT_Done_Face(f->face); delete f;
 return name;
}

int main(int argc, char **argv) {
 if (argc != 3 || !vita_ttf_ensure_library()) return 2;
 SharedFontStatePrivate priv; SharedFontState sfs{&priv};
 check(sfs.initUserFonts(argv[2]) == 0 && priv.userFonts.empty(), "a missing folder adds nothing");
 check(sfs.initUserFonts("") == 0, "no folder configured adds nothing");
 int faces = sfs.initUserFonts(argv[1]);
 char what[160];
 snprintf(what, sizeof what, "4 faces registered from 3 font files (got %d), notes.txt skipped", faces);
 check(faces == 4, what);

 const char *jpGothic = "\xef\xbc\xad\xef\xbc\xb3 \xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf";
 const char *jpPGothic = "\xef\xbc\xad\xef\xbc\xb3 \xef\xbc\xb0\xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf";
 check(user(priv, "ms gothic") && user(priv, "ms gothic")->index == 0, "MS Gothic = face 0 of msgothic.ttc");
 check(user(priv, jpGothic) && user(priv, jpGothic)->index == 0, "its Japanese name \xef\xbc\xad\xef\xbc\xb3 \xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf is registered too");
 check(user(priv, "ms pgothic") && user(priv, "ms pgothic")->index == 1, "MS PGothic = face 1 of the same file");
 check(user(priv, jpPGothic) != nullptr, "\xef\xbc\xad\xef\xbc\xb3 \xef\xbc\xb0\xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf registered");
 check(user(priv, "umeplus gothic") && user(priv, "umeplus gothic")->regular, "the regular face wins over the bold one");

 std::string f0 = faceFamily(user(priv, "ms gothic")), f1 = faceFamily(user(priv, "ms pgothic"));
 snprintf(what, sizeof what, "face 0 opens as MS Gothic with the Vita TTF backend (got %s)", f0.c_str());
 check(f0 == "MS Gothic", what);
 snprintf(what, sizeof what, "face 1 opens as MS PGothic with the Vita TTF backend (got %s)", f1.c_str());
 check(f1 == "MS PGothic", what);

 check(sfs.fontPresent("MS PGothic") && sfs.fontPresent(jpGothic), "Font.exist? sees user fonts");
 std::string out;
 pickExistingFontName({"No Such Font", "MS PGothic"}, out, sfs, true);
 check(out == "MS PGothic", "a game's name list picks the user font");
 out.clear();
 pickExistingFontName({"UmePlus Gothic", "MS Gothic", "Courier New"}, out, sfs, false);
 check(out == "UmePlus Gothic", "a real user font also serves the engine's default list (RGSS2)");
 priv.subs.m["ms gothic"] = "game font";
 check(!sfs.fontPresent("MS Gothic"), "a per-game fontSub still decides");
 priv.subs.m.clear();
 for (VitaTtfBlob &blob : vita_ttf_blobs) free(blob.data);  /* the cache keeps them by design */
 printf(failures ? "FAILED (%d)\n" : "all passed\n", failures);
 return failures ? 1 : 0;
}
'''


def main():
    stubs = (ROOT / "src/vita_stubs.cpp").read_text()
    backend = stubs[stubs.index("struct TTF_Font\n"):stubs.index("/* =================================================================\n * pixman")]
    parts = [
        ff.block(FONT, "struct FontSet\n", ";"),
        STATE,
        ff.block(FONT, "static std::string decodeSfntName("),
        ff.block(FONT, "static const char *builtinFallbackFor("),
        ff.block(FONT, "static bool bundledFallbackPackAvailable()"),
        ff.block(FONT, "static bool isRegularStyle("),
        ff.block(FONT, "int SharedFontState::initUserFonts("),
        ff.block(FONT, "bool SharedFontState::fontPresent("),
        ff.block(FONT, "static bool pickExistingFontName("),
    ]
    host = {k: v for k, v in os.environ.items() if not k.startswith("PKG_CONFIG_")}
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2", "freetype2"], text=True, env=host))
    with tempfile.TemporaryDirectory(prefix="hardrpg-user-fonts-") as name:
        fonts = Path(name, "fonts")
        fonts.mkdir()
        collection = TTCollection()
        collection.fonts = [renamed(ROOT / "assets/kosugi.ttf", "MS Gothic", "ＭＳ ゴシック"),
                            renamed(ROOT / "assets/liberation.ttf", "MS PGothic", "ＭＳ Ｐゴシック")]
        collection.save(fonts / "msgothic.ttc")
        renamed(ROOT / "assets/liberation.ttf", "UmePlus Gothic", style="Bold").save(fonts / "a-umeplus-bold.ttf")
        renamed(ROOT / "assets/liberation.ttf", "UmePlus Gothic").save(fonts / "z-umeplus.ttf")
        (fonts / "notes.txt").write_text("not a font\n")
        cpp = Path(name, "check.cpp")
        cpp.write_text(ff.PREAMBLE + EXTRA + backend + "".join(parts) + CHECKS)
        exe = Path(name, "check")
        subprocess.run(["c++", "-std=gnu++17", "-O1", "-g", "-D__vita__", "-fsanitize=address,undefined",
                        str(cpp), *flags, "-o", str(exe)], check=True)
        return subprocess.run([str(exe), str(fonts), str(Path(name, "missing"))]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
