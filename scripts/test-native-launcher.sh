#!/usr/bin/env bash
# Builds the desktop launcher host and runs every launcher test: the UI and
# library suite, the artwork downloader (canned replies, no network) and the
# cover cache. Usage: test-native-launcher.sh WORK_DIR
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${1:?usage: test-native-launcher.sh WORK_DIR}"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
"$ROOT/scripts/build-native-launcher.sh" "$WORK/hardrpg-native"
LIBS="$(pkg-config --cflags --libs libpng libturbojpeg libwebp)"
"${CXX:-c++}" -std=c++14 -O2 -I"$ROOT/src" "$ROOT/tests/art-fetch.cpp" "$ROOT/src/native_launcher/art_fetch.cpp" \
    "$ROOT/src/native_launcher/image.cpp" $LIBS $(pkg-config --cflags --libs libcurl) -pthread -o "$WORK/art-fetch-tests"
"${CXX:-c++}" -std=c++14 -O2 -I"$ROOT/src" "$ROOT/tests/art-loader.cpp" "$ROOT/src/native_launcher/art_loader.cpp" \
    "$ROOT/src/native_launcher/image.cpp" $LIBS -pthread -o "$WORK/art-loader-tests"
for suite in art-fetch art-loader; do
    rm -rf "$WORK/$suite" && mkdir -p "$WORK/$suite"
    "$WORK/$suite-tests" "$WORK/$suite"
done
python3 "$ROOT/tests/native-launcher.py" "$WORK/hardrpg-native" "$WORK"
