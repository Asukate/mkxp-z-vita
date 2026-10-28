#!/usr/bin/env bash
# Package the launcher backend recorded in the verified engine build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS=""
OUT=""
TITLE_ID="HARDRPG01"
PROFILE="real-vita"
EASY_WS=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) printf 'usage: %s [--ws DIR] [--profile real-vita|vita3k] [--out DIR] [--title-id ID] [--easyrpg-ws DIR]\n' "$0"; exit 0;;
        --ws|--profile|-j|--out|--title-id|--easyrpg-ws)
            [[ $# -ge 2 && "$2" != --* && -n "$2" ]] || { printf 'missing value for %s\n' "$1" >&2; exit 2; } ;;
    esac
    case "$1" in
        --easyrpg-ws) EASY_WS="$2"; shift 2;;
        --ws) WS="$2"; shift 2;;
        --out) OUT="$2"; shift 2;;
        --title-id) TITLE_ID="$2"; shift 2;;
        --profile) PROFILE="$2"; shift 2;;
        *) echo "usage: $0 [--ws DIR] [--out DIR] [--title-id ID] [--profile real-vita|vita3k]" >&2; exit 2;;
    esac
done
[[ "$TITLE_ID" =~ ^[A-Z0-9]{9}$ ]] || { echo 'TITLE_ID must be 9 uppercase letters/digits' >&2; exit 2; }
[[ "$PROFILE" == real-vita || "$PROFILE" == vita3k ]] || { echo 'invalid profile' >&2; exit 2; }
WS="${WS:-$ROOT/build/$PROFILE}"
OUT="${OUT:-$WS/packages}"
VITASDK="${VITASDK:?set VITASDK to the VitaSDK root}"
WS="$(realpath -m "$WS")"
OUT="$(realpath -m "$OUT")"
python3 "$ROOT/scripts/build-state.py" dependencies "$WS" "$PROFILE" --verify
python3 "$ROOT/scripts/build-state.py" engine "$WS" "$PROFILE" --verify
BIN="$VITASDK/bin"
ELF="$WS/build/mkxp-z-parity-$PROFILE/mkxp-z"
[[ -s "$ELF" ]] || { echo "missing engine ELF: $ELF" >&2; exit 1; }
for tool in arm-vita-eabi-strip vita-elf-create vita-make-fself vita-mksfoex vita-pack-vpk; do
    [[ -x "$BIN/$tool" ]] || { echo "missing tool: $BIN/$tool" >&2; exit 1; }
done

mkdir -p "$OUT"
STAGE="$(mktemp -d "$OUT/.launcher-stage.XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
"$BIN/arm-vita-eabi-strip" --strip-debug "$ELF" -o "$STAGE/eboot-stripped.elf"
"$BIN/vita-elf-create" "$STAGE/eboot-stripped.elf" "$STAGE/eboot.velf"
# Compressed: the kernel reads 9 MB instead of 24 and loads the whole
# image at once; LoadExec into a game measured 0.7 s faster on the Vita.
"$BIN/vita-make-fself" -c "$STAGE/eboot.velf" "$STAGE/eboot.bin"
[[ "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["native_launcher"])' "$WS/engine-state.json")" == True ]] ||
    { echo 'the engine was built without the HardRPG launcher' >&2; exit 1; }
APP_VER="$(python3 - "$ROOT/src/native_launcher/release.h" <<'PY2'
import re,sys
text=open(sys.argv[1]).read()
match=re.search(r'^#define HARDRPG_APP_VER "([0-9]{2}\.[0-9]{2})"$',text,re.M)
if not match: raise SystemExit('missing release app version')
print(match[1])
PY2
)"
# Extended memory mode. The engine's 160 MiB heap, the Fiber stacks inside the
# RGSS thread and vitaGL's pools do not fit the default budget (about 109 MiB
# less); EasyRPG's Vita package needs the same mode.
MEMORY_ARGS=(-d ATTRIBUTE2=12)
# One release bubble (HARDRPG01) and test bubbles that cannot be mistaken
# for it: any other Title ID is named "HardRPG TEST", gets the TEST artwork
# and shows a build stamp on its LiveArea. A Test version is never packaged
# as the release.
VERSION="$(python3 -c 'import re,sys; print(re.search(r"^#define HARDRPG_VERSION \"([^\"]+)\"$", open(sys.argv[1]).read(), re.M)[1])' "$ROOT/src/native_launcher/release.h")"
if [[ "$TITLE_ID" == HARDRPG01 ]]; then
    [[ "$VERSION" != *Test* ]] || { echo "refusing to package $VERSION as the HARDRPG01 release" >&2; exit 1; }
    APP_NAME='HardRPG'; ART="$ROOT/launcher/livearea"
    LIVEAREA_ARGS=(--release "$ROOT/src/native_launcher/release.h")
else
    APP_NAME='HardRPG TEST'; ART="$ROOT/launcher/livearea-test"
    STAMP="$(git -C "$ROOT" rev-parse --short=8 HEAD)"
    if ! git -C "$ROOT" diff --quiet HEAD 2>/dev/null; then
        STAMP="$STAMP+$(git -C "$ROOT" diff HEAD | sha256sum | cut -c1-6)"
    fi
    LIVEAREA_ARGS=(--version "$VERSION | TEST $STAMP $(date -u '+%m-%d %H:%M') UTC")
fi
"$BIN/vita-mksfoex" "${MEMORY_ARGS[@]}" -s "TITLE_ID=$TITLE_ID" -s "APP_VER=$APP_VER" "$APP_NAME" "$STAGE/param.sfo"
python3 "$ROOT/scripts/build-livearea.py" "$ROOT/launcher/livearea/template.xml" \
    "$STAGE/template.xml" "${LIVEAREA_ARGS[@]}"
LAUNCHER_ARGS=(-a "$ROOT/launcher/native-backend.txt=launcher-backend.txt")
# Home screen: title font, icons and brand marks (rendered from SVG).
LAUNCHER_ARGS+=(-a "$ROOT/launcher/font-display.ttf=font-display.ttf" -a "$ROOT/LICENSES/Rajdhani.txt=licenses/Rajdhani.txt" -a "$ROOT/LICENSES/Vita-illustration.txt=licenses/Vita-illustration.txt")
for ui in "$ROOT"/launcher/ui/*.png; do
    LAUNCHER_ARGS+=(-a "$ui=ui/$(basename "$ui")")
done
# Interface sounds (made by launcher/art/tools/make_sfx.py).
for sfx in "$ROOT"/launcher/sfx/*.ogg; do
    LAUNCHER_ARGS+=(-a "$sfx=sfx/$(basename "$sfx")")
done

EASY_ARGS=()
if [[ -n "$EASY_WS" ]]; then
    EASY_WS="$(realpath "$EASY_WS")"
    python3 "$ROOT/scripts/easyrpg-state.py" verify "$EASY_WS"
    EASY_ARGS=(-a "$EASY_WS/runtime/easyrpg.bin=easyrpg.self"
               -a "$EASY_WS/runtime/manifest.json=runtime/easyrpg-manifest.json")
    for notice in "$EASY_WS"/runtime/licenses/*; do
        EASY_ARGS+=(-a "$notice=licenses/easyrpg/$(basename "$notice")")
    done
fi

VPK="$OUT/HardRPG-$TITLE_ID-$PROFILE.vpk"
(cd "$STAGE" && "$BIN/vita-pack-vpk" \
    -s param.sfo -b eboot.bin \
    -a "$ART/icon0.png=sce_sys/icon0.png" \
    -a "$ART/pic0.png=sce_sys/pic0.png" \
    -a "$ART/bg0.png=sce_sys/livearea/contents/bg0.png" \
    -a "$ART/startup.png=sce_sys/livearea/contents/startup.png" \
    -a template.xml=sce_sys/livearea/contents/template.xml \
    -a "$ROOT/launcher/mkxp.json=mkxp.json" \
    -a "$ROOT/launcher/legacy_ruby_compat.rb=legacy_ruby_compat.rb" \
    -a "$ROOT/launcher/time_strftime_guard.rb=time_strftime_guard.rb" \
    -a "$ROOT/launcher/performance_patch.rb=performance_patch.rb" \
    -a "$ROOT/launcher/sprite_fastpath.rb=sprite_fastpath.rb" \
    -a "$ROOT/launcher/native_events.rb=native_events.rb" \
    -a "$ROOT/launcher/vita_controls.rb=vita_controls.rb" \
    -a "$ROOT/launcher/vita_tap_to_walk.rb=vita_tap_to_walk.rb" \
    -a "$ROOT/launcher/crash_rescue.rb=crash_rescue.rb" \
    -a "$ROOT/assets/ca-certificates.crt=ca-certificates.crt" \
    -a "$ROOT/assets/TimGM6mb.sf2=TimGM6mb.sf2" \
    -a "$ROOT/LICENSES/FluidLite.txt=licenses/FluidLite.txt" \
    -a "$ROOT/LICENSES/TimGM6mb.txt=licenses/TimGM6mb.txt" \
    -a "$ROOT/assets/liberation.ttf=font.ttf" \
    -a "$ROOT/launcher/assets/hardrpg-splash.png=stub/Graphics/Pictures/hardrpg-splash.png" \
    -a "$ROOT/THIRD-PARTY-NOTICES.md=THIRD-PARTY-NOTICES.md" \
    -a "$ROOT/COPYING=licenses/GPL-2.0.txt" \
    -a "$ROOT/LICENSES/GPL-3.0.txt=licenses/GPL-3.0.txt" \
    -a "$ROOT/LICENSES/LGPL-3.0.txt=licenses/LGPL-3.0.txt" \
    -a "$ROOT/LICENSES/Liberation.txt=licenses/Liberation.txt" \
    -a "$ROOT/LICENSES/math-neon.txt=licenses/math-neon.txt" \
    -a "$ROOT/LICENSES/curl.txt=licenses/curl.txt" \
    -a "$ROOT/LICENSES/MPL-2.0.txt=licenses/MPL-2.0.txt" \
    -a "$ROOT/src/display/libnsgif/COPYING=licenses/libnsgif.txt" \
    "${LAUNCHER_ARGS[@]}" \
    "${EASY_ARGS[@]}" \
    "$VPK")
python3 "$ROOT/scripts/verify-launcher-payload.py" "$VPK" "$TITLE_ID"
sha256sum "$VPK"
