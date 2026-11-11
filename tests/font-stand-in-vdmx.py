#!/usr/bin/env python3
"""Bundled Liberation fonts size like the Windows fonts they replace (font.cpp stand_in_VDMX).
RPG Maker XP's Arial at size 22 is 19 ppem on Windows (VDMX); linear scaling gives 20, which made
Mysta's Vore RPG message lines run past the 448-pixel message box."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = (ROOT / "src/display/font.cpp").read_text()
CONTAINER = SOURCE[SOURCE.index("typedef struct {\n\tTTF_Font *font;"):SOURCE.index("} Font_Container;") + len("} Font_Container;")]
FUNC = SOURCE[SOURCE.index("static int stand_in_VDMX"):]
FUNC = FUNC[:FUNC.index("\n}\n") + 3]
PROGRAM = r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "core-font-vdmx.h"
struct TTF_Font;
struct FT_FaceRec { const char *family_name; };
using FT_Face = FT_FaceRec *;
''' + CONTAINER + "\n" + FUNC + r'''
static int ppem(const char *family, int height, Font_Container *out = nullptr) {
    FT_FaceRec face = {family};
    struct Handle { FT_Face face; } handle = {&face};
    Font_Container c = {};
    c.font = reinterpret_cast<TTF_Font *>(&handle);
    int r = stand_in_VDMX(&c, height);
    if (out) *out = c;
    return r;
}
int main() {
    int failures = 0;
    auto expect = [&](const char *f, int h, int want) {
        int got = ppem(f, h);
        if (got != want) { std::printf("%s height %d: ppem %d, want %d\n", f, h, got, want); ++failures; }
    };
    expect("Liberation Sans", 22, 19);   /* RGSS1 default (Arial 22), measured under RGSS102E */
    expect("Liberation Sans", 24, 21);   /* RGSS2 default size 24 */
    expect("DejaVu Sans", 22, 0);        /* other fonts keep linear scaling */
    expect("Liberation Sans", 0, 0);
    /* Every height picks a size whose box fits it, and sizes never shrink as height grows. */
    const char *families[] = {"Liberation Sans", "Liberation Serif", "Liberation Mono"};
    for (const char *f : families) {
        int last = 0;
        for (int h = 12; h <= 200; ++h) {
            Font_Container c;
            int p = ppem(f, h, &c);
            if (!p || c.yMax - c.yMin > h || p < last) { std::printf("%s height %d: ppem %d box %d\n", f, h, p, c.yMax - c.yMin); ++failures; }
            last = p;
        }
    }
    std::printf("failures=%d\n", failures);
    return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="hardrpg-stand-in-vdmx-") as d:
    cpp = Path(d) / "t.cpp"
    cpp.write_text(PROGRAM)
    subprocess.run(["c++", "-std=c++14", "-O2", "-I" + str(ROOT / "src/display"), str(cpp), "-o", str(Path(d) / "t")], check=True)
    raise SystemExit(subprocess.run([str(Path(d) / "t")]).returncode)
