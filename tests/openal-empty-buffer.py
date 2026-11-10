#!/usr/bin/env python3
"""Build and run tests/openal-empty-buffer.c against the production Vita mixer.

Only the Vita OpenAL and SDL_sound headers are staged and SDL2 comes from the
host. Ogg decoding is not exercised, so a stub vorbisfile.h stands in for
libvorbis (the CI image does not ship it). See the C file for what is checked
(issue 25).
"""
from pathlib import Path
import shlex
import shutil
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent

VORBIS_STUB = r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int64_t ogg_int64_t;
typedef struct { int channels; long rate; } vorbis_info;
typedef struct { int unused; } OggVorbis_File;
typedef struct {
    size_t (*read_func)(void *, size_t, size_t, void *);
    int (*seek_func)(void *, ogg_int64_t, int);
    int (*close_func)(void *);
    long (*tell_func)(void *);
} ov_callbacks;
static inline int ov_open_callbacks(void *s, OggVorbis_File *f, const char *i, long n, ov_callbacks c)
{ (void)s; (void)f; (void)i; (void)n; (void)c; return -1; }
static inline vorbis_info *ov_info(OggVorbis_File *f, int l) { (void)f; (void)l; return 0; }
static inline long ov_read(OggVorbis_File *f, char *b, int n, int e, int w, int s, int *bs)
{ (void)f; (void)b; (void)n; (void)e; (void)w; (void)s; (void)bs; return 0; }
static inline int ov_clear(OggVorbis_File *f) { (void)f; return 0; }
'''


def pkg(*args):
    return shlex.split(subprocess.check_output(["pkg-config", *args], text=True, env={k: v for k, v in os.environ.items() if not k.startswith("PKG_CONFIG_")}))


def main():
    with tempfile.TemporaryDirectory() as tmp:
        inc = Path(tmp, "inc")
        inc.mkdir()
        for name in ("al.h", "alc.h", "alext.h"):
            shutil.copy(ROOT / "deps/vita-compat" / name, inc / name)
        shutil.copy(ROOT / "deps/vendor/vita-sdl2-shim/include/SDL2/SDL_sound.h", inc / "SDL_sound.h")
        (inc / "vorbis").mkdir()
        (inc / "vorbis/vorbisfile.h").write_text(VORBIS_STUB)
        exe = Path(tmp, "openal-empty-buffer")
        cmd = ["cc", "-std=gnu11", "-O1", "-w", f"-I{inc}", f"-I{ROOT / 'deps'}",
               *pkg("--cflags", "sdl2"), str(ROOT / "tests/openal-empty-buffer.c"),
               *pkg("--libs", "sdl2"), "-lpthread", "-lm", "-o", str(exe)]
        subprocess.run(cmd, check=True)
        return subprocess.run([str(exe)]).returncode


if __name__ == "__main__":
    sys.exit(main())
