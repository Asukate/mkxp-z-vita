#!/usr/bin/env bash
# HardRPG Vita engine build.
#
# Usage:
#   VITASDK=/opt/vitasdk ./scripts/build-vita.sh [--ws <dir>] [--profile real-vita|vita3k] [-j N]
#
# Requires a completed bootstrap-vita.sh in the same workspace.
# Configures the Vita engine and leaves a verified ELF for packaging.
set -euo pipefail

WS=""; PROFILE="real-vita"; JOBS="4"
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) printf 'usage: %s [--ws DIR] [--profile real-vita|vita3k] [-j N]\n' "$0"; exit 0;;
        --ws|--profile|-j|--out|--title-id)
            [[ $# -ge 2 && "$2" != --* && -n "$2" ]] || { printf 'missing value for %s\n' "$1" >&2; exit 2; } ;;
    esac
    case "$1" in
        --ws) WS="$2"; shift 2;;
        --profile) PROFILE="$2"; shift 2;;
        -j) JOBS="$2"; shift 2;;
        *) printf 'usage: %s [--ws DIR] [--profile real-vita|vita3k] [-j N]\n' "$0" >&2; exit 2;;
    esac
done
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo 'jobs must be a positive integer' >&2; exit 2; }
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CAND_SRC="$(cd "$SCRIPT_DIR/.." && pwd)"
if [[ -z "$WS" ]]; then WS="$CAND_SRC/build/$PROFILE"; fi
WS="$(realpath -m "$WS")"
python3 "$SCRIPT_DIR/build-state.py" dependencies "$WS" "$PROFILE" --verify
[[ -f "$WS/bootstrap-complete" ]] || { echo 'run bootstrap-vita.sh to completion first' >&2; exit 1; }
[[ "$PROFILE" == real-vita || "$PROFILE" == vita3k ]] || { printf 'ERROR: profile\n' >&2; exit 1; }

PREFIX="$WS/prefix"; BUILD="$WS/build/mkxp-z-parity-$PROFILE"
[[ -f "$PREFIX/lib/libruby-3.1.a" ]] || { printf 'ERROR: run bootstrap-vita.sh first (no libruby in %s)\n' "$PREFIX" >&2; exit 1; }
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
# find_library() calls in meson get no -L from pkg-config; point the
# compiler driver at the workspace prefix (standard LIBRARY_PATH mechanism,
# no absolute paths baked into tracked files).
export PATH="${VITASDK:?VITASDK must be set}/bin:$PATH"
export VITA_DEP_PREFIX="$PREFIX"
# find_library() deps (bz2, iconv, charset) carry no -L of their
# own. The Vita driver ignores LIBRARY_PATH, so layer a workspace-only
# overlay cross file that appends -L<prefix>/lib to the tracked link args.
# (Full flag sets repeated here so behavior is identical whether meson
# merges or replaces across --cross-file layers.)
OVERLAY="$WS/build/ws-paths.ini"
mkdir -p "$(dirname "$OVERLAY")"
cat > "$OVERLAY" <<EOF2
[built-in options]
c_args = ['-marm', '-mcpu=cortex-a9', '-mfpu=neon', '-mfloat-abi=hard', '-O3', '-D__linux__', '-D__vita__', '-D__psp2__']
c_link_args = ['-Wl,-q', '-L$PREFIX/lib']
cpp_args = ['-marm', '-mcpu=cortex-a9', '-mfpu=neon', '-mfloat-abi=hard', '-O3', '-D__linux__', '-D__vita__', '-D__psp2__']
cpp_link_args = ['-Wl,-q', '-Wl,--allow-multiple-definition', '-L$PREFIX/lib']
EOF2

if [[ ! -f "$BUILD/build.ninja" ]]; then
    meson setup "$BUILD" "$CAND_SRC" \
        --cross-file="$CAND_SRC/linux/meson-vita.txt" \
        --cross-file="$OVERLAY" \
        -Dbuildtype=release -Denable-https=false \
        -Dshared_fluid=false -Dvita_diagnostics=false -Dproto_launcher=true \
        -Dvita_libdir="$PREFIX/lib" \
        -Dvita_sdk_libdir="$VITASDK/arm-vita-eabi/lib"
else
    meson setup --reconfigure "$BUILD" "$CAND_SRC" -Dvita_diagnostics=false -Dproto_launcher=true
fi
ninja -C "$BUILD" -j"$JOBS"
python3 "$SCRIPT_DIR/build-state.py" engine "$WS" "$PROFILE"
printf '=== BUILD %s complete ===\n' "$PROFILE"
ls -lh "$BUILD/mkxp-z"
sha256sum "$BUILD/mkxp-z"
