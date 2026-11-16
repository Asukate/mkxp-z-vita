#!/usr/bin/env python3
"""A tall tileset's atlas packs only the rows the map uses, and every tile
still samples its own pixels.

Exit Fate's world map tileset is 256x14784 (issue 31): the whole-tileset atlas
is 1280x4096 (20 MB) and long sessions ran out of graphics memory entering the
world map, leaving it invisible or ending the game. Compile the production
TileAtlas (src/display/gl/tileatlas.cpp) and the tilemap's
updateAtlasInfo/packUsedRows/packedBlits (src/display/tilemap.cpp) against
small stand-ins, lay a fake atlas out with the computed blits and check that
the atlas position of every tile the map uses holds that tile's pixels.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
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


def line(source, text):
    start = source.index(text)
    return source[start:source.index("\n", start)] + "\n"


ETC = r'''
#pragma once
#include <cstddef>
using std::size_t;
struct Vec2i { int x = 0, y = 0; Vec2i() {} Vec2i(int x, int y) : x(x), y(y) {} };
'''

STUBS = r'''
#include "etc-internal.h"
#include "tileatlas.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <vector>
struct Exception { enum Type { MKXPError }; Exception(Type, const char *m) { throw std::runtime_error(m); } };
struct Table {
 int xs, ys, zs; std::vector<int16_t> d;
 Table(int x, int y, int z) : xs(x), ys(y), zs(z), d(x * y * z) {}
 int xSize() const { return xs; } int ySize() const { return ys; } int zSize() const { return zs; }
 const int16_t &at(int x, int y, int z) const { return d[xs * ys * z + xs * y + x]; }
 int16_t &at(int x, int y, int z) { return d[xs * ys * z + xs * y + x]; }
};
struct Bitmap { int h; int height() const { return h; } };
static bool nullOrDisposed(const Bitmap *b) { return !b; }
static struct { struct { int maxTexSize = 4096; } caps; } glState;
struct TilemapPrivate {
 Bitmap *tileset = nullptr; Table *mapData = nullptr;
 struct { Vec2i size; int efTilesetH = 0, layoutH = 0; bool packed = false;
          std::vector<int16_t> rowMap; std::vector<int> packedRows; } atlas;
'''

TAIL = r'''
};

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* Lay the atlas out: each atlas pixel row remembers which tileset pixel row it
 * holds (lane x matters only per 256-px lane, tiles span one lane). */
static void check(const char *name, int tsH, const std::vector<int> &tiles, bool expectPacked, int maxBytes)
{
 Bitmap ts{tsH}; Table map(20, 15, 3);
 for (size_t i = 0; i < tiles.size(); ++i) map.at(i % 20, (i / 20) % 15, i / 300) = tiles[i];
 TilemapPrivate p; p.tileset = &ts; p.mapData = &map;
 p.updateAtlasInfo();
 const Vec2i sz = p.atlas.size;
 CHECK(p.atlas.packed == expectPacked, "%s packed=%d", name, p.atlas.packed);
 CHECK((long)sz.x * sz.y * 4 <= maxBytes, "%s atlas %dx%d too big", name, sz.x, sz.y);
 TileAtlas::BlitVec blits = TileAtlas::calcBlits(p.atlas.layoutH, sz);
 if (p.atlas.packed) blits = p.packedBlits(blits);
 std::vector<int> src((size_t)sz.x / 256 * sz.y, -1);
 for (auto &b : blits) {
  CHECK(b.dst.x % 256 == 0 && b.h > 0 && b.dst.y + b.h <= sz.y, "%s bad blit", name);
  CHECK(b.src.y >= 0 && b.src.y + b.h <= p.atlas.efTilesetH, "%s blit reads past tileset", name);
  for (int y = 0; y < b.h; ++y) {
   int &cell = src[(size_t)(b.dst.x / 256) * sz.y + b.dst.y + y];
   CHECK(cell == -1, "%s atlas row written twice", name);
   cell = b.src.y + y;
  }
 }
 std::set<int> seen;
 for (int t : tiles) {
  if (t < 384) continue;
  int row = (t - 384) / 8, col = (t - 384) % 8;
  if (row * 32 >= p.atlas.efTilesetH || !seen.insert(t).second) continue;
  int layoutRow = row;
  if (p.atlas.packed) { CHECK(p.atlas.rowMap[row] >= 0, "%s row %d not packed", name, row); layoutRow = p.atlas.rowMap[row]; }
  Vec2i pos = TileAtlas::tileToAtlasCoor(col, layoutRow, p.atlas.layoutH, sz.y);
  CHECK(pos.x / 256 * 256 + col * 32 == pos.x, "%s tile %d lane", name, t);
  for (int y = 0; y < 32; ++y)
   CHECK(src[(size_t)(pos.x / 256) * sz.y + pos.y + y] == row * 32 + y,
         "%s tile %d atlas row %d holds %d", name, t, pos.y + y, src[(size_t)(pos.x / 256) * sz.y + pos.y + y]);
 }
 printf("%s: tileset 256x%d -> atlas %dx%d (%.1f MB) packed=%d rows=%zu\n", name, tsH, sz.x, sz.y,
        sz.x * sz.y * 4 / 1048576.0, p.atlas.packed, p.atlas.packed ? p.atlas.packedRows.size() : (size_t)tsH / 32);
}

static std::vector<int> rows(std::initializer_list<int> r)
{
 std::vector<int> t; for (int row : r) for (int c = 0; c < 8; c += 3) t.push_back(384 + row * 8 + c); return t;
}

int main()
{
 /* Exit Fate world map: tiles from scattered rows up to row 152, autotiles, empty cells. */
 std::vector<int> wm = rows({0, 1, 2, 5, 6, 7, 8, 30, 31, 40, 41, 42, 43, 90, 100, 101, 152});
 wm.push_back(48); wm.push_back(0); wm.push_back(383);
 check("world-map", 14784, wm, true, 5 * 1048576);
 /* Small tileset: unchanged whole-tileset atlas. */
 check("town", 1792, rows({0, 3, 20, 55}), false, 5 * 1048576);
 /* Tall but under the threshold (Vanaheim 4096): unchanged. */
 check("tall-4096", 4096, rows({0, 127}), false, 8 * 1048576);
 /* Tiles past the end of the tileset are ignored, not packed. */
 std::vector<int> past = rows({1, 2});
 past.push_back(384 + 470 * 8);
 check("past-end", 14784, past, true, 4 * 1048576);
 /* A map with no tileset tiles still gets a valid one-row layout. */
 check("autotiles-only", 14784, {48, 100, 0}, true, 4 * 1048576);
 /* Every row used: one blit run per lane, full contents. */
 std::vector<int> all; for (int r = 0; r < 300; ++r) all.push_back(384 + r * 8);
 check("all-rows", 9600, all, true, 17 * 1048576);
 if (fails) { printf("%d failures\n", fails); return 1; }
 printf("OK\n");
 return 0;
}
'''


def main():
    body = STUBS
    body += line(TILEMAP, "static const size_t packAtlasBytes")
    body += block(TILEMAP, "void updateAtlasInfo()")
    body += block(TILEMAP, "void packUsedRows()")
    body += block(TILEMAP, "TileAtlas::BlitVec packedBlits(")
    body += TAIL
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "etc-internal.h").write_text(ETC)
        (tmp / "test.cpp").write_text(body)
        exe = tmp / "test"
        subprocess.run(["c++", "-std=c++14", "-O1", "-Wall", "-I", str(tmp), "-I", str(ROOT / "src/display/gl"),
                        str(tmp / "test.cpp"), str(ROOT / "src/display/gl/tileatlas.cpp"), "-o", str(exe)],
                       check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        print(result.stdout, end="")
        if result.returncode:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
