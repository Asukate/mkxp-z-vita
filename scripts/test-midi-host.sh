#!/usr/bin/env bash
# Run the production sequencer/synth against captured PCM on the host.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AREA="${1:-$ROOT/build/host-midi}"
mkdir -p "$AREA/source"
ARCHIVE="$AREA/fluidlite.tar.gz"
if [[ ! -f "$ARCHIVE" ]]; then
    wget -q https://api.github.com/repos/divideconcept/FluidLite/tarball/4a01cf1c67419e71da971d209f2855bbf4f3bab8 -O "$ARCHIVE"
fi
printf '%s  %s\n' e0f7b6789021ef55c6f51ca5b82a1f9274564b4936ae6960f5db64ff4d6ef1ea "$ARCHIVE" | sha256sum -c -
if [[ ! -f "$AREA/source/CMakeLists.txt" ]]; then tar -xzf "$ARCHIVE" --strip-components=1 -C "$AREA/source"; fi
cmake -S "$AREA/source" -B "$AREA/build" -DFLUIDLITE_BUILD_STATIC=ON -DFLUIDLITE_BUILD_SHARED=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$AREA/build" -j 4
"${CXX:-c++}" -std=c++14 -O1 -g -fsanitize=address,undefined -DMKXPZ_VITA_FLUIDLITE \
    -I "$ROOT/src" -I "$ROOT/src/audio" -I "$ROOT/src/util" \
    -I "$AREA/build" -I "$AREA/source/include" $(pkg-config --cflags sdl2) \
    "$ROOT/tests/midi-runtime.cpp" "$ROOT/src/audio/fluid-fun.cpp" \
    "$AREA/build/libfluidlite.a" $(pkg-config --libs sdl2) -lm -o "$AREA/midi-tests"
"$AREA/midi-tests" "$ROOT/assets/TimGM6mb.sf2"
