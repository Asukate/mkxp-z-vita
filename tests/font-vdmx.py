#!/usr/bin/env python3
"""Exercise production font sizing with the Vita SDL headers and SFNT bytes."""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=ROOT / "src/display/font.cpp")
SOURCE = parser.parse_args().source.read_text()
INCLUDES = "\n".join(re.findall(r"^#include <SDL[^>]+>", SOURCE, re.M))
SIZING = SOURCE[SOURCE.index("typedef struct {\n\tTTF_Font *font;"):
                SOURCE.index("/* Some fonts have large usWinDescent")]
PREAMBLE = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
// Only the SFNT table-read boundary is faked; font.cpp parses the raw bytes.
using FT_ULong = unsigned long;
using FT_Long = long;
using FT_Byte = unsigned char;
using FT_Error = int;
struct FT_FaceRec { long face_flags; };
using FT_Face = FT_FaceRec *;
constexpr long FT_FACE_FLAG_SFNT = 8;
#define FT_IS_SFNT(face) ((face)->face_flags & FT_FACE_FLAG_SFNT)
static std::vector<unsigned char> table;
static bool available = true;
extern "C" FT_Error FT_Load_Sfnt_Table(FT_Face, FT_ULong tag, FT_Long offset,
                                      FT_Byte *buffer, FT_ULong *length) {
    if (!available || tag != 0x56444d58UL || offset < 0 ||
        static_cast<size_t>(offset) > table.size()) return 1;
    size_t remaining = table.size() - offset;
    if (!buffer) { *length = remaining; return 0; }
    if (*length > remaining) return 1;
    std::memcpy(buffer, table.data() + offset, *length);
    return 0;
}
'''
CHECKS = r'''
int main() {
    // One 1:1 ratio, one group, three records. All SFNT words are big endian.
    table = {0,0, 0,1, 0,1, 1,1,1,1, 0,12,
             0,3, 19,21, 0,19, 0,24, 255,250,
             0,20, 0,26, 255,250, 0,21, 0,27, 255,250};
    FT_FaceRec face = {};
    face.face_flags = FT_FACE_FLAG_SFNT;
    struct Handle { FT_Face face; } handle = {&face};
    int failures = 0, cases = 0;
    for (const auto &test : std::vector<std::pair<int,int>>{{32,20},{31,19},{33,21},{29,0}}) {
        Font_Container font = {};
        font.font = reinterpret_cast<TTF_Font *>(&handle);
        int actual = load_VDMX(&font, test.first);
        ++cases;
        if (actual != test.second) {
            std::printf("height=%d ppem=%d expected=%d\n", test.first, actual, test.second);
            ++failures;
        }
        if (test.first == 32 && (font.yMax != 26 || font.yMin != -6)) {
            std::printf("metrics=%d,%d expected=26,-6\n", font.yMax, font.yMin);
            ++failures;
        }
    }
    available = false;
    Font_Container missing = {};
    missing.font = reinterpret_cast<TTF_Font *>(&handle);
    ++cases;
    if (load_VDMX(&missing, 32) != 0) ++failures;
    std::printf("cases=%d failures=%d\n", cases, failures);
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="hardrpg-vdmx-") as directory:
    path = Path(directory)
    cpp = path / "vdmx.cpp"
    cpp.write_text(INCLUDES + "\n" + PREAMBLE + SIZING + CHECKS)
    subprocess.run(["c++", "-std=c++14", "-O2",
                    "-I" + str(ROOT / "deps/vendor/vita-sdl2-shim/include/SDL2"),
                    str(cpp), "-o", str(path / "vdmx")], check=True)
    raise SystemExit(subprocess.run([str(path / "vdmx")]).returncode)
