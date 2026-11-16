#!/usr/bin/env bash
# Build the PC-98 runtime (NP2kai, MIT) embedded in HardRPG packages as
# app0:/np2kai.self. Output: <ws>/runtime/np2kai.self and licenses/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS=""; JOBS=4
while [[ $# -gt 0 ]]; do
    case "$1" in
        --ws) WS="$2"; shift 2;;
        -j) JOBS="$2"; shift 2;;
        -h|--help) echo 'usage: build-np2kai-vita.sh [--ws DIR] [-j N]'; exit 0;;
        *) echo "unknown argument: $1" >&2; exit 2;;
    esac
done
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo 'jobs must be positive' >&2; exit 2; }
VITASDK="${VITASDK:?set VITASDK}"
export PATH="$VITASDK/bin:$PATH"
WS="$(realpath -m "${WS:-$ROOT/build/np2kai}")"
mkdir -p "$WS/upstream" "$WS/build" "$WS/runtime/licenses"

# libretro's NP2kai fork, the one RetroArch builds for the Vita.
NP2KAI=1ea561cd7fd62b33f4e7b3f1c2e30fc3e6089f28
SRC="$WS/upstream/NP2kai"
if [[ ! -d "$SRC" ]]; then
    git init -q "$SRC"
    git -C "$SRC" remote add origin https://github.com/libretro/NP2kai.git
    git -C "$SRC" fetch --depth=1 origin "$NP2KAI"
    git -C "$SRC" checkout -q --detach FETCH_HEAD
fi
[[ "$(git -C "$SRC" rev-parse HEAD)" == "$NP2KAI" ]] || { echo "unexpected NP2kai revision in $SRC" >&2; exit 1; }

# HardRPG's one change to the core: a switch the frontend uses to skip
# drawing a frame when the Vita falls behind (the machine still runs it).
python3 - "$SRC/sdl/libretro/libretro.c" <<'PATCH'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_bytes().decode()
if 'np2kai_hardrpg_draw' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    edits = [
        ('void retro_run (void)' + nl + '{',
         '/* HardRPG: the frontend can skip drawing a frame when it falls behind. */' + nl +
         'int np2kai_hardrpg_draw = 1;' + nl + nl + 'void retro_run (void)' + nl + '{'),
        ('      pccore_exec(true /*draw*/);',
         '      pccore_exec(np2kai_hardrpg_draw ? TRUE : FALSE /*draw*/);'),
        ('   if(draw32bit) {' + nl + '      video_cb(FrameBuffer, w, h, w * 4/*Pitch*/);',
         '   if(menuvram == NULL && !np2kai_hardrpg_draw) {' + nl +
         '      video_cb(NULL, w, h, 0); /* picture unchanged */' + nl +
         '   } else if(draw32bit) {' + nl + '      video_cb(FrameBuffer, w, h, w * 4/*Pitch*/);'),
    ]
    for old, new in edits:
        if s.count(old) != 1:
            sys.exit('NP2kai patch does not apply: ' + old.splitlines()[0])
        s = s.replace(old, new)
    p.write_bytes(s.encode())
PATCH

# The core's own makefile has a Vita target (static archive, no threads).
# It builds at -O2 for generic ARM; the Vita's Cortex-A9 runs the x86
# interpreter faster as ARM code at -O3.
CORE="$SRC/sdl/np2kai_libretro_vita.a"
CORE_FLAGS=("PLATFORM_DEFINES=-DVITA -fno-short-enums -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fomit-frame-pointer"
            "COMMONFLAGS=-O3 -DNDEBUG -D_NDEBUG")
STAMP="$WS/build/core.stamp"
if [[ ! -s "$CORE" || "$(cat "$STAMP" 2>/dev/null)" != "${CORE_FLAGS[*]} draw-switch" ]]; then
    make -C "$SRC/sdl" -f Makefile.libretro platform=vita clean >/dev/null 2>&1 || true
    make -C "$SRC/sdl" -f Makefile.libretro platform=vita -j "$JOBS" \
        NP2KAI_GIT_TAG=rev.15 "${CORE_FLAGS[@]}" >"$WS/build/core.log" 2>&1 ||
        { tail -20 "$WS/build/core.log" >&2; exit 1; }
    echo "${CORE_FLAGS[*]} draw-switch" >"$STAMP"
fi

OBJ="$WS/build/obj"; mkdir -p "$OBJ"
INC=(-I"$SRC/sdl/libretro/libretro-common/include" -I"$ROOT/src" -I"$ROOT/backends/np2kai")
# -fno-short-enums matches the core, which passes enums across the boundary.
FLAGS=(-O2 -mcpu=cortex-a9 -mfpu=neon -fno-short-enums -ffunction-sections -fdata-sections
       "-ffile-prefix-map=$WS=/work" "-ffile-prefix-map=$ROOT=/source")
# The core's static Vita build expects RetroArch to provide libretro-common;
# HardRPG is the frontend here, so build the parts the core calls.
COMMON=()
for f in compat/compat_strl.c compat/compat_posix_string.c compat/compat_strcasestr.c \
         compat/fopen_utf8.c encodings/encoding_utf.c features/features_cpu.c \
         file/file_path.c file/file_path_io.c file/retro_dirent.c \
         streams/file_stream.c streams/file_stream_transforms.c \
         string/stdstring.c vfs/vfs_implementation.c; do
    o="$OBJ/common-$(echo "$f" | tr / -).o"
    arm-vita-eabi-gcc "${FLAGS[@]}" -DVITA -D__LIBRETRO__ -I"$SRC/sdl/libretro/libretro-common/include" \
        -c "$SRC/sdl/libretro/libretro-common/$f" -o "$o"
    COMMON+=("$o")
done
arm-vita-eabi-gcc "${FLAGS[@]}" "${INC[@]}" -c "$ROOT/backends/np2kai/frontend.c" -o "$OBJ/frontend.o"
arm-vita-eabi-g++ "${FLAGS[@]}" "${INC[@]}" -std=gnu++17 -c "$ROOT/backends/np2kai/vita_main.cpp" -o "$OBJ/vita_main.o"
arm-vita-eabi-g++ -Wl,-q -Wl,--gc-sections -o "$WS/build/np2kai.elf" \
    "$OBJ/vita_main.o" "$OBJ/frontend.o" "$CORE" "${COMMON[@]}" \
    -lvita2d -lpng -ljpeg -lz -lSceGxm_stub -lSceDisplay_stub -lSceSysmodule_stub \
    -lSceCommonDialog_stub -lScePgf_stub -lSceCtrl_stub -lSceAudio_stub \
    -lScePower_stub -lSceAppMgr_stub -lSceAppUtil_stub -lm -lstdc++
arm-vita-eabi-strip -g "$WS/build/np2kai.elf" -o "$WS/build/np2kai.stripped.elf"
vita-elf-create "$WS/build/np2kai.stripped.elf" "$WS/build/np2kai.velf" >/dev/null
vita-make-fself -c "$WS/build/np2kai.velf" "$WS/runtime/np2kai.self" >/dev/null
cp "$SRC/LICENSE" "$WS/runtime/licenses/NP2kai.txt"
python3 - "$WS/runtime" "$NP2KAI" <<'PY'
import hashlib, json, pathlib, sys
rt = pathlib.Path(sys.argv[1])
self_ = (rt / 'np2kai.self').read_bytes()
json.dump({'np2kai': sys.argv[2], 'sha256': hashlib.sha256(self_).hexdigest(),
           'bytes': len(self_)}, open(rt / 'manifest.json', 'w'), indent=1)
PY
ls -la "$WS/runtime/np2kai.self"
