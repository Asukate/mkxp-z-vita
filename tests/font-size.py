#!/usr/bin/env python3
"""Check production font-size rounding and Vita text measurement/rendering.

The default fixture uses the bundled font with controlled height metrics.
Pass additional font paths to check their real metrics and glyph advances.
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
PREAMBLE = raster.PREAMBLE + "\n#include FT_TRUETYPE_TABLES_H\n"
CHECKS = r'''
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FT_Library library;
    if (FT_Init_FreeType(&library)) return 2;
    FT_Face face;
    if (FT_New_Face(library, argv[1], 0, &face)) return 2;
    auto *os2 = static_cast<TT_OS2 *>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
    auto *hhea = static_cast<TT_HoriHeader *>(FT_Get_Sfnt_Table(face, FT_SFNT_HHEA));
    if (!os2 || !hhea) return 2;
    const auto originalEm = face->units_per_EM;
    const TT_OS2 originalOS2 = *os2;
    const TT_HoriHeader originalHhea = *hhea;
    TTF_Font handle = {};
    handle.face = face;
    handle.library = library;
    handle.hinting = 3;
    Font_Container font = {};
    font.font = &handle;
    int cases = 0, failures = 0;
    auto expect = [&](int height, int expected) {
        int actual = calc_ppem_for_height(&font, height);
        ++cases;
        if (actual != expected) {
            std::printf("height=%d ppem=%d expected=%d\n", height, actual, expected);
            ++failures;
        }
    };

    // These metrics put size 14 just beyond a whole-pixel height boundary.
    face->units_per_EM = 1024;
    os2->usWinAscent = 448;
    os2->usWinDescent = 128;
    expect(14, 24);
    expect(9, 16);
    expect(13, 23);
    expect(0, 28);
    expect(-14, 14);
    expect(-65535, 65535);
    expect(-65536, 1);
    expect(36864, 1);
    for (int height = 1; height <= 100; ++height) {
        int ppem = calc_ppem_for_height(&font, height);
        ++cases;
        if (int64_t(576) * ppem > int64_t(height) * 1024 ||
            int64_t(576) * (ppem + 1) <= int64_t(height) * 1024) {
            std::printf("height=%d ppem=%d does not fit maximally\n", height, ppem);
            ++failures;
        }
    }
    os2->usWinDescent = 0xff80; // Signed -128 in the unsigned descent field.
    expect(14, 24);
    os2->usWinAscent = os2->usWinDescent = 0;
    hhea->Ascender = 448;
    hhea->Descender = -128;
    expect(14, 24);
    face->units_per_EM = 65535;
    os2->usWinAscent = 32767;
    expect(65534, 1); // The size exceeds MAX_PPEM; products exceed signed 32 bits.

    face->units_per_EM = originalEm;
    *os2 = originalOS2;
    *hhea = originalHhea;
    int ppem = calc_ppem_for_height(&font, 14);
    if (TTF_SetFontSize(&handle, ppem)) return 2;
    int width = 0, advances = 0;
    if (TTF_SizeUTF8(&handle, "0123456789", &width, nullptr)) return 2;
    for (char digit = '0'; digit <= '9'; ++digit) {
        const char text[] = {digit, 0};
        int advance;
        if (TTF_SizeUTF8(&handle, text, &advance, nullptr)) return 2;
        advances += advance;
    }
    auto *image = vita_ttf_render(&handle, "0123456789", SDL_Color{255,255,255,255});
    if (!image) return 2;
    ++cases;
    if (image->w != width) ++failures;
    SDL_FreeSurface(image);
    if (face->family_name && std::strcmp(face->family_name, "Coder's Crux") == 0) {
        ++cases;
        if (ppem != 24 || width != 90 || advances != 90) ++failures;
    }
    std::printf("family=%s ppem=%d digit_width=%d digit_advances=%d cases=%d failures=%d\n",
                face->family_name, ppem, width, advances, cases, failures);
    vita_ttf_forget_glyphs(&handle);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return failures ? 1 : 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fonts", nargs="*", type=Path)
    parser.add_argument("--source", type=Path, default=ROOT / "src/display/font.cpp")
    args = parser.parse_args()
    source = args.source.read_text()
    sizing = source[source.index("typedef struct {\n\tTTF_Font *font;"):
                    source.index("/* /wine */")]
    stubs = (ROOT / "src/vita_stubs.cpp").read_text()
    backend = stubs[stubs.index("struct TTF_Font\n"):
                    stubs.index("/* =================================================================\n * pixman")]
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "sdl2", "freetype2"], text=True))
    with tempfile.TemporaryDirectory(prefix="hardrpg-font-size-") as directory:
        path = Path(directory)
        cpp = path / "size.cpp"
        cpp.write_text(PREAMBLE + backend + sizing + CHECKS)
        executable = path / "size"
        subprocess.run(["c++", "-std=c++17", "-O2",
                        str(cpp), *flags, "-o", str(executable)], check=True)
        failed = False
        for font in args.fonts or [ROOT / "assets/liberation.ttf"]:
            print(f"Font: {font}", flush=True)
            failed |= subprocess.run([str(executable), str(font.resolve())]).returncode != 0
        raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
