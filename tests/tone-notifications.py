#!/usr/bin/env python3
"""Check the real Tone setters used by RGSS window updates."""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SOURCE = r'''
#include "etc.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
void expect(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
int main() try {
    Tone tone(10, 20, 30, 40);
    unsigned changed = 0;
    tone.valueChanged.connect([&] { ++changed; });
    for (int i = 0; i < 120; ++i) tone.set(10, 20, 30, 40);
    expect(changed == 0, "unchanged window tone must not invalidate its texture");
    tone.set(10, 20, 30, 41);
    expect(changed == 1 && tone.gray == 41 && tone.norm.w == float(41.0/255),
           "changed tone must update normalized components and emit once");
    Tone same(10, 20, 30, 41);
    tone = same;
    tone = tone;
    expect(changed == 1, "equal and self assignments must not invalidate");
    Tone other(-10, 200, 300, -5);
    tone = other;
    expect(changed == 2 && tone == other && tone.norm.z == 1 && tone.norm.w == 0,
           "different assignment must preserve raw and normalized values");
    tone.setRed(-10); tone.setGreen(200); tone.setBlue(300); tone.setGray(-5);
    expect(changed == 2, "equal component setters must not invalidate");
    tone.setRed(12); tone.setGreen(-500); tone.setBlue(301); tone.setGray(500);
    expect(changed == 6 && tone.red == 12 && tone.green == -500 &&
           tone.blue == 301 && tone.gray == 500 && tone.norm.x == float(12.0/255) &&
           tone.norm.y == -1 && tone.norm.z == 1 && tone.norm.w == 1,
           "changed raw values must emit even if clamping gives the same result");
    tone.set(0, 0, 0, 0);
    expect(changed == 7 && !tone.hasEffect(), "reset must remain observable");
    std::cout << "PASS: unchanged window tones avoid notifications; changed tones stay observable\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
'''
with tempfile.TemporaryDirectory(prefix="hardrpg-tone-") as folder:
    work = Path(folder)
    cpp = work / "tone.cpp"
    cpp.write_text(SOURCE)
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "sdl2"], text=True))
    subprocess.run([
        "c++", "-std=c++17", "-D__vita__", "-ffunction-sections", "-fdata-sections",
        "-I" + str(ROOT / "src/etc"), "-I" + str(ROOT / "src/util"),
        *flags, str(cpp), str(ROOT / "src/etc/etc.cpp"),
        "-Wl,--gc-sections", "-o", str(work / "tone")
    ], check=True)
    subprocess.run([str(work / "tone")], check=True)
