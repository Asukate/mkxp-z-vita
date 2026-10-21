#!/usr/bin/env bash
# Build the EasyRPG runtime embedded in native HardRPG packages.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS=""; PREFIX=""; JOBS=4
while [[ $# -gt 0 ]]; do
    case "$1" in
        --ws|--prefix|-j)
            [[ $# -ge 2 && -n "$2" && "$2" != --* ]] || { echo "missing value for $1" >&2; exit 2; };;
    esac
    case "$1" in
        --ws) WS="$2"; shift 2;;
        --prefix) PREFIX="$2"; shift 2;;
        -j) JOBS="$2"; shift 2;;
        -h|--help) echo 'usage: build-easyrpg-vita.sh --prefix EASYRPG_DEPENDENCIES [--ws DIR] [-j N]'; exit 0;;
        *) echo "unknown argument: $1" >&2; exit 2;;
    esac
done
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo 'jobs must be positive' >&2; exit 2; }
VITASDK="${VITASDK:?set VITASDK}"
[[ -n "$PREFIX" ]] || { echo 'provide an EasyRPG Vita dependency prefix; see docs/easyrpg.md' >&2; exit 2; }
PREFIX="$(realpath "$PREFIX")"
WS="$(realpath -m "${WS:-$ROOT/build/easyrpg}")"
[[ "$WS" != *'"'* && "$WS" != *';'* && "$PREFIX" != *'"'* && "$PREFIX" != *';'* ]] || exit 2
mkdir -p "$WS/upstream" "$WS/build" "$WS/runtime"
export PATH="$VITASDK/bin:$PATH"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
unset PKG_CONFIG_PATH
PLAYER=78328fa29f465315291e161130e6682f69410370
LCF=92c4450a1bc1acb58bd02bbb99b57e5036919cdf
fetch_source() {
    local name="$1" revision="$2" dest="$3"
    if [[ ! -d "$dest" ]]; then
        git init -q "$dest"
        git -C "$dest" remote add origin "https://github.com/EasyRPG/$name.git"
        git -C "$dest" fetch --depth=1 origin "$revision"
        git -C "$dest" checkout --detach FETCH_HEAD
    fi
    [[ "$(git -C "$dest" rev-parse HEAD)" == "$revision" && -z "$(git -C "$dest" status --porcelain)" ]] || {
        echo "upstream source has changed: $dest" >&2; exit 1;
    }
}
fetch_source Player "$PLAYER" "$WS/upstream/Player"
fetch_source liblcf "$LCF" "$WS/upstream/liblcf"
INPUTS="$(python3 - "$ROOT" <<'PY'
import hashlib, pathlib, sys
root=pathlib.Path(sys.argv[1]); h=hashlib.sha256()
for path in ['backends/easyrpg/main.cpp','src/native_launcher/easyrpg_handoff.h',
             'src/util/json5pp.hpp','deps/patches/easyrpg/0.8.1.1-hardrpg.patch',
             'scripts/build-easyrpg-vita.sh']:
    h.update((root/path).read_bytes())
print(h.hexdigest())
PY
)"
SOURCE="$WS/build/Player-$INPUTS"
if [[ ! -d "$SOURCE" ]]; then
    STAGE="$(mktemp -d "$WS/build/.player.XXXXXX")"
    trap 'rm -rf -- "$STAGE"' EXIT
    git -C "$WS/upstream/Player" archive HEAD | tar xf - -C "$STAGE"
    patch -d "$STAGE" -p1 < "$ROOT/deps/patches/easyrpg/0.8.1.1-hardrpg.patch"
    cp "$ROOT/backends/easyrpg/main.cpp" "$STAGE/src/platform/psvita/main.cpp"
    mv "$STAGE" "$SOURCE"
    trap - EXIT
fi
TOOLCHAIN="$WS/build/toolchain.cmake"
cat > "$TOOLCHAIN" <<EOF
include("\$ENV{VITASDK}/share/vita.toolchain.cmake")
list(PREPEND CMAKE_FIND_ROOT_PATH "$PREFIX")
EOF
COMMON=(-G Ninja "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN" "-DCMAKE_INSTALL_PREFIX=$PREFIX"
        "-DCMAKE_PREFIX_PATH=$PREFIX" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
        "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=$WS=/work -ffile-prefix-map=$ROOT=/source -ffile-prefix-map=$PREFIX=/dependencies")
cmake -S "$WS/upstream/liblcf" -B "$WS/build/liblcf" "${COMMON[@]}" \
    -DLIBLCF_WITH_XML=OFF -DLIBLCF_WITH_ICU=ON -DLIBLCF_ENABLE_TESTS=OFF -DLIBLCF_ENABLE_TOOLS=OFF
cmake --build "$WS/build/liblcf" -j "$JOBS"
cmake --install "$WS/build/liblcf"
cmake -S "$SOURCE" -B "$WS/build/player-$INPUTS" "${COMMON[@]}" \
    "-DHARDRPG_SOURCE_DIR=$ROOT" "-DPLAYER_PREFIX_PATH_APPEND=$PREFIX" \
    -DPLAYER_FIND_ROOT_PATH_APPEND=ON -DPLAYER_WITH_NLOHMANN_JSON=OFF \
    -DPLAYER_VERSION_APPEND=HardRPG
cmake --build "$WS/build/player-$INPUTS" --target easyrpg-player -j "$JOBS"
arm-vita-eabi-strip --strip-debug "$WS/build/player-$INPUTS/easyrpg-player.elf" -o "$WS/runtime/easyrpg-stripped.elf"
vita-elf-create "$WS/runtime/easyrpg-stripped.elf" "$WS/runtime/easyrpg.velf"
# Compressed (-c): 6.6 MB instead of 19 MB to read on every launch.
vita-make-fself -s -c "$WS/runtime/easyrpg.velf" "$WS/runtime/easyrpg.bin"
python3 "$ROOT/scripts/easyrpg-state.py" record "$WS" "$PREFIX" --source "$SOURCE"
sha256sum "$WS/runtime/easyrpg.bin"
