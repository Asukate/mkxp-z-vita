#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:?usage: build-native-launcher.sh OUTPUT}"
mkdir -p "$(dirname "$OUT")"
"${CXX:-g++}" -std=c++14 -O2 -DMKXPZ_PROTO_LAUNCHER -I"$ROOT/src" \
    "$ROOT/src/native_launcher/model.cpp" "$ROOT/src/native_launcher/ui.cpp" \
    "$ROOT/src/native_launcher/view.cpp" "$ROOT/src/native_launcher/home.cpp" "$ROOT/src/native_launcher/home_view.cpp" "$ROOT/src/native_launcher/image.cpp" "$ROOT/src/native_launcher/art_loader.cpp" "$ROOT/src/native_launcher/art_fetch.cpp" "$ROOT/src/native_launcher/sheet_view.cpp" "$ROOT/src/native_launcher/welcome.cpp" "$ROOT/src/native_launcher/welcome_view.cpp" "$ROOT/src/native_launcher/i18n.cpp" "$ROOT/src/native_launcher/host.cpp" \
    "$ROOT/src/native_launcher/text_input.cpp" "$ROOT/src/native_launcher/update_check.cpp" \
    "$ROOT/src/native_launcher/runtime.cpp" "$ROOT/src/vita_proto_launcher.cpp" \
    $(pkg-config --cflags --libs physfs freetype2 libpng libturbojpeg libwebp sdl2 glesv2 libcurl) -pthread -o "$OUT"
