#!/usr/bin/env bash
# Build the Dante 98 runtime (OpenDT, MIT; ymfm, BSD-3-Clause) embedded in
# HardRPG packages as app0:/opendt.self. Output: <ws>/runtime/opendt.self,
# manifest.json and licenses/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS=""; JOBS=4; SOURCE=""; DEPS=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --ws) WS="$2"; shift 2;;
        -j) JOBS="$2"; shift 2;;
        # A local clone to fetch from instead of GitHub (offline builds).
        --source) SOURCE="$2"; shift 2;;
        # The engine workspace (scripts/build-vita.sh): its prefix/ and
        # release-network/prefix/ hold the libraries the in-game menu needs.
        --deps-ws) DEPS="$2"; shift 2;;
        -h|--help) echo 'usage: build-opendt-vita.sh --deps-ws ENGINE_WS [--ws DIR] [-j N] [--source GIT_DIR]'; exit 0;;
        *) echo "unknown argument: $1" >&2; exit 2;;
    esac
done
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo 'jobs must be positive' >&2; exit 2; }
VITASDK="${VITASDK:?set VITASDK}"
[[ -n "$DEPS" ]] || { echo 'build-opendt-vita.sh needs --deps-ws (the engine workspace)' >&2; exit 2; }
PREFIX="$(realpath "$DEPS/prefix")"; NETWORK="$(realpath "$DEPS/release-network/prefix")"
[[ -f "$PREFIX/lib/libfreetype.a" && -f "$NETWORK/lib/libcurl.a" ]] ||
    { echo "missing engine libraries under $DEPS" >&2; exit 2; }
export PATH="$VITASDK/bin:$PATH"
WS="$(realpath -m "${WS:-$ROOT/build/opendt}")"
mkdir -p "$WS/upstream" "$WS/build" "$WS/runtime/licenses"

OPENDT=48e86578903eb7d0d2b0c2d28cc215a7933bfea3
SRC="$WS/upstream/OpenDT"
PATCH="$ROOT/backends/opendt/opendt-hardrpg.patch"
if [[ ! -d "$SRC" ]]; then
    git init -q "$SRC"
    git -C "$SRC" remote add origin "${SOURCE:-https://github.com/MaxSignal/OpenDT.git}"
    git -C "$SRC" fetch -q --depth=1 origin "$OPENDT"
fi
# The patch is applied to a clean checkout each time, so editing it rebuilds.
git -C "$SRC" checkout -q --force --detach "$OPENDT" 2>/dev/null ||
    { git -C "$SRC" fetch -q --depth=1 origin "$OPENDT" && git -C "$SRC" checkout -q --force --detach "$OPENDT"; }
git -C "$SRC" clean -qfdx
git -C "$SRC" apply "$PATCH"

OBJ="$WS/build/obj"; mkdir -p "$OBJ"
FLAGS=(-O2 -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -ffunction-sections -fdata-sections
       -DNDEBUG "-ffile-prefix-map=$WS=/work" "-ffile-prefix-map=$ROOT=/source")
INC=(-I"$SRC/src" -I"$SRC/third_party/ymfm/src" -I"$ROOT/src" -I"$PREFIX/include" -I"$PREFIX/include/freetype2" -I"$PREFIX/include/SDL2")
SOURCES=("$SRC"/src/core/*.cpp "$SRC"/src/music/*.cpp "$SRC"/src/d1/*.cpp "$SRC"/src/d2/*.cpp
         "$SRC/src/app/games.cpp"
         "$SRC"/third_party/ymfm/src/ymfm_{opn,ssg,adpcm,misc}.cpp
         "$ROOT/backends/opendt/vita_main.cpp")
# The in-game menu is the launcher's own page for the game, as in the main
# engine (src/meson.build lists the same files).
LAUNCHER=(model ui save_backups view home home_view image art_loader art_fetch sheet_view welcome welcome_view
          i18n text_input update_check theme game_menu keyboard_view)
for f in "${LAUNCHER[@]}"; do SOURCES+=("$ROOT/src/native_launcher/$f.cpp"); done
# The Vita's pthread library has no pthread_once, which libstdc++'s guards
# for function-local statics need once threads are on (pthread_cancel below).
C_SOURCES=("$ROOT/src/vita_pthread_once.c")
OBJECTS=()
for f in "${SOURCES[@]}"; do
    OBJECTS+=("$OBJ/$(basename "$(dirname "$f")")-$(basename "${f%.cpp}").o")
done
# Every object is rebuilt each time (a full build takes about a minute).
compile() {
    local w=(); [[ "$1" == */third_party/* ]] && w=(-w)
    [[ "$1" == "$ROOT"/src/native_launcher/* ]] && w=(-w -DMKXPZ_PROTO_LAUNCHER -DMKXPZ_NATIVE_LAUNCHER)
    arm-vita-eabi-g++ "${FLAGS[@]}" "${INC[@]}" -std=gnu++17 "${w[@]}" -c "$1" -o "$2" ||
        { echo "failed: $1" >&2; return 1; }
}
for f in "${C_SOURCES[@]}"; do
    arm-vita-eabi-gcc "${FLAGS[@]}" -c "$f" -o "$OBJ/c-$(basename "${f%.c}").o"
    OBJECTS+=("$OBJ/c-$(basename "${f%.c}").o")
done
for i in "${!SOURCES[@]}"; do
    while (( $(jobs -rp | wc -l) >= JOBS )); do wait -n || exit 1; done
    compile "${SOURCES[$i]}" "${OBJECTS[$i]}" &
done
while (( $(jobs -rp | wc -l) > 0 )); do wait -n || exit 1; done
wait || exit 1
link() {
    local L="$VITASDK/arm-vita-eabi/lib"
    # pthread_cancel: without it libstdc++ thinks threads do not exist and
    # std::mutex never locks (the launcher code uses threads).
    arm-vita-eabi-g++ -Wl,-q -Wl,--gc-sections -Wl,-u,pthread_cancel -o "$WS/build/opendt.elf" "${OBJECTS[@]}" "$@" \
        -Wl,--start-group "$PREFIX/lib/libphysfs.a" "$PREFIX/lib/libfreetype.a" "$PREFIX/lib/libpng16.a" \
        "$PREFIX/lib/libz.a" "$PREFIX/lib/libbz2.a" "$PREFIX/lib/libiconv.a" "$NETWORK/lib/libcurl.a" "$L/libturbojpeg.a" \
        "$L/libwebp.a" "$L/libsharpyuv.a" "$L/libmbedtls.a" "$L/libmbedx509.a" "$L/libmbedcrypto.a" \
        -lvita2d -ljpeg -lSceGxm_stub -lSceDisplay_stub -lSceSysmodule_stub \
        -lSceCommonDialog_stub -lScePgf_stub -lSceCtrl_stub -lSceTouch_stub -lSceAudio_stub \
        -lScePower_stub -lSceAppMgr_stub -lSceAppUtil_stub -lSceRtc_stub -lSceNet_stub \
        -lSceNetCtl_stub -lSceIme_stub -lSceKernelThreadMgr_stub -lpthread -lm -lstdc++ -Wl,--end-group
}
link
# vita-elf-create puts a few KB after the code; when the data segment starts
# closer than that it fails ("segment 1 overlaps"). Then link again with 4 KB
# of padding after the code, which moves the data segment to the next page.
GAP="$(arm-vita-eabi-readelf -lW "$WS/build/opendt.elf" | python3 -c '
import sys
load = [l.split() for l in sys.stdin if l.split()[:1] == ["LOAD"]]
print(int(load[1][2], 16) - int(load[0][2], 16) - int(load[0][4], 16))')"
if (( GAP < 4096 )); then
    echo 'const char hardrpg_segment_pad[4096] = {1};' |
        arm-vita-eabi-gcc -x c -c - -o "$OBJ/segment-pad.o"
    link "$OBJ/segment-pad.o" -Wl,--undefined=hardrpg_segment_pad
fi
python3 "$ROOT/tests/vita-launcher-threading.py" "$VITASDK/bin/arm-vita-eabi-nm" "$WS/build/opendt.elf"
arm-vita-eabi-strip -g "$WS/build/opendt.elf" -o "$WS/build/opendt.stripped.elf"
vita-elf-create "$WS/build/opendt.stripped.elf" "$WS/build/opendt.velf" >/dev/null
vita-make-fself -c "$WS/build/opendt.velf" "$WS/runtime/opendt.self" >/dev/null
cp "$SRC/LICENSE" "$WS/runtime/licenses/OpenDT.txt"
cp "$SRC/third_party/ymfm/LICENSE" "$WS/runtime/licenses/ymfm.txt"
python3 - "$WS/runtime" "$OPENDT" "$PATCH" <<'PY'
import hashlib, json, pathlib, sys
rt = pathlib.Path(sys.argv[1])
self_ = (rt / 'opendt.self').read_bytes()
json.dump({'opendt': sys.argv[2],
           'patch_sha256': hashlib.sha256(pathlib.Path(sys.argv[3]).read_bytes()).hexdigest(),
           'sha256': hashlib.sha256(self_).hexdigest(), 'bytes': len(self_)},
          open(rt / 'manifest.json', 'w'), indent=1)
PY
ls -la "$WS/runtime/opendt.self"
