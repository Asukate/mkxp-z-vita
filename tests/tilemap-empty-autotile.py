#!/usr/bin/env python3
"""Tiles of an empty or missing autotile slot draw nothing, as in RGSS.

Exit Fate (issue 23) places tiles 336-383 (autotile slot 7) over its sky and
sea panoramas while that slot's name is empty, so RPG::Cache hands the tilemap
Bitmap.new(32, 32). The Vita atlas upload copied that slot's texture memory and
the sky showed noise. Compile the production Bitmap::isKnownTransparent
(src/display/bitmap.cpp) and the tilemap's updateAutotileInfo/handleAutotile
(src/display/tilemap.cpp) against small stand-ins and check which slots get
uploaded and drawn.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
BITMAP = (ROOT / "src/display/bitmap.cpp").read_text()
TILEMAP = (ROOT / "src/display/tilemap.cpp").read_text()


def block(source, signature):
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
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
struct Vec4 { float x = 0, y = 0, z = 0, w = 0;
 bool operator==(const Vec4 &o) const { return x == o.x && y == o.y && z == o.z && w == o.w; } };
struct BitmapPrivate {
 SDL_Surface *megaSurface = nullptr, *cpu = nullptr;
 struct { bool enabled = false; } animation;
 bool pendingClear = false; Vec4 pendingColor;
};
struct Bitmap {
 BitmapPrivate *p = nullptr; bool disposed = false; int w = 32, h = 32;
 bool isDisposed() const { return disposed; }
 bool hasHires() const { return false; }
 SDL_Surface *megaSurface() const { return p->megaSurface; }
 int width() const { return w; } int height() const { return h; }
 bool isKnownTransparent() const;
};
static bool nullOrDisposed(const Bitmap *b) { return !b || b->isDisposed(); }
'''

TILEMAP_STUBS = r'''
struct FloatRect { float x, y, w, h; FloatRect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {} };
typedef FloatRect StaticRect;
struct SVertex { float x, y, u, v; };
typedef std::vector<SVertex> SVVector;
namespace Quad { static void setTexPosRect(SVertex *v, const FloatRect &, const FloatRect &) { for (int i = 0; i < 4; ++i) v[i] = SVertex(); } }
static const int autotileCount = 7, autotileW = 96, autotileH = 128;
static StaticRect autotileRects[48 * 4] = {
#define R FloatRect(0, 0, 16, 16)
#define R8 R, R, R, R, R, R, R, R
 R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8, R8
#undef R8
#undef R
};
static void atSelectSubPos(FloatRect &, int) {}
struct TilemapPrivate {
 Bitmap *autotiles[autotileCount] = {};
 bool buffersDirty = false;
 struct { bool animated = false; } tiles;
 struct {
  std::vector<uint8_t> usableATs, animatedATs;
  bool smallATs[autotileCount] = {false};
  bool drawnATs[autotileCount] = {false};
  int nATFrames[autotileCount] = {1};
 } atlas;
'''

CHECKS = r'''
};
static int failures = 0;
static void check(bool ok, const char *what) { printf("%s: %s\n", ok ? "ok" : "FAIL", what); failures += !ok; }
static std::vector<SDL_Surface *> surfaces;
static SDL_Surface *zeroed(int w, int h) {
 surfaces.push_back(SDL_CreateRGBSurface(0, w, h, 32, 0xff, 0xff00, 0xff0000, 0xff000000));
 return surfaces.back();
}
static size_t verts(TilemapPrivate &t, int tile) { SVVector v; t.handleAutotile(0, 0, tile, &v); return v.size(); }

int main() {
 /* Bitmap::isKnownTransparent */
 BitmapPrivate cpuP; cpuP.cpu = zeroed(32, 32);
 Bitmap blank; blank.p = &cpuP;
 check(blank.isKnownTransparent(), "Bitmap.new(32, 32) in memory is known transparent");
 BitmapPrivate inkP; inkP.cpu = zeroed(32, 32);
 ((uint8_t *)inkP.cpu->pixels)[inkP.cpu->pitch * 31 + 31 * 4 + 3] = 1;
 Bitmap ink; ink.p = &inkP;
 check(!ink.isKnownTransparent(), "one non-zero byte (last pixel) is not transparent");
 BitmapPrivate gpuP; Bitmap gpu; gpu.p = &gpuP; gpu.w = 96; gpu.h = 128;
 check(!gpu.isKnownTransparent(), "a GPU bitmap with no pending clear is unknown (drawn)");
 gpuP.pendingClear = true;
 check(gpu.isKnownTransparent(), "a pending clear to transparent is known transparent");
 gpuP.pendingClear = false;
 BitmapPrivate animP; animP.cpu = zeroed(32, 32); animP.animation.enabled = true;
 Bitmap anim; anim.p = &animP;
 check(!anim.isKnownTransparent(), "animated bitmaps are never treated as blank");

 /* Tilemap: slot 0 real 3x4 autotile, slot 1 real 1-row (small) autotile,
  * slot 5 missing, slot 6 empty name -> Bitmap.new(32, 32). */
 BitmapPrivate smallP; smallP.cpu = zeroed(128, 32); ((uint8_t *)smallP.cpu->pixels)[3] = 255;
 Bitmap small; small.p = &smallP; small.w = 128; small.h = 32;
 TilemapPrivate t;
 t.autotiles[0] = &gpu; t.autotiles[1] = &small; t.autotiles[6] = &blank;
 t.updateAutotileInfo();
 check(t.atlas.usableATs == std::vector<uint8_t>({0, 1}), "the empty slot is not uploaded into the atlas");
 check(verts(t, 48) == 16, "a real 3x4 autotile tile still draws 4 pieces");
 check(verts(t, 96) == 4, "a real 1-row autotile tile still draws");
 check(verts(t, 6 * 48 + 48) == 0, "tile 336 (empty slot 7) draws nothing");
 check(verts(t, 383) == 0, "tile 383 (empty slot 7) draws nothing");
 check(verts(t, 5 * 48 + 48) == 0, "a tile of a missing slot draws nothing");
 check(t.buffersDirty, "first info pass marks the quads for rebuild");

 t.buffersDirty = false;
 t.updateAutotileInfo();
 check(!t.buffersDirty, "an unchanged atlas rebuild keeps the quads");
 ((uint8_t *)cpuP.cpu->pixels)[7] = 9;   /* the game draws into the empty slot */
 t.updateAutotileInfo();
 check(t.buffersDirty && verts(t, 336) == 4, "drawing into the empty slot brings its tiles back");

 TilemapPrivate onlyBlank; onlyBlank.autotiles[6] = &anim; anim.disposed = true;
 BitmapPrivate b2; b2.cpu = zeroed(32, 32); Bitmap blank2; blank2.p = &b2; onlyBlank.autotiles[2] = &blank2;
 onlyBlank.updateAutotileInfo();
 check(!onlyBlank.tiles.animated, "a blank 1-row slot does not make the map animate");
 for (SDL_Surface *s : surfaces) SDL_FreeSurface(s);
 printf(failures ? "FAILED (%d)\n" : "all passed\n", failures);
 return failures ? 1 : 0;
}
'''


def main():
    source = (PREAMBLE
              + block(BITMAP, "bool Bitmap::isKnownTransparent() const")
              + TILEMAP_STUBS
              + block(TILEMAP, "\tvoid updateAutotileInfo()")
              + block(TILEMAP, "\tvoid handleAutotile(int x, int y, int tileInd, SVVector *array)")
              + CHECKS)
    flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2"], text=True, env={k: v for k, v in os.environ.items() if not k.startswith("PKG_CONFIG_")}).split()
    with tempfile.TemporaryDirectory(prefix="hardrpg-tilemap-empty-at-") as name:
        cpp = Path(name, "check.cpp")
        cpp.write_text(source)
        exe = Path(name, "check")
        subprocess.run(["c++", "-std=c++17", "-O1", "-g", "-Wall", "-Wno-unused-function",
                        "-fsanitize=address,undefined", str(cpp), *flags, "-o", str(exe)], check=True)
        return subprocess.run([str(exe)]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
