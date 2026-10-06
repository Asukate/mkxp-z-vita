#!/usr/bin/env bash
# Package the launcher backend recorded in the verified engine build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS=""
OUT=""
TITLE_ID="HARDRPG01"
PROFILE="real-vita"
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) printf 'usage: %s [--ws DIR] [--profile real-vita|vita3k] [--out DIR] [--title-id ID]\n' "$0"; exit 0;;
        --ws|--profile|-j|--out|--title-id)
            [[ $# -ge 2 && "$2" != --* && -n "$2" ]] || { printf 'missing value for %s\n' "$1" >&2; exit 2; } ;;
    esac
    case "$1" in
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
"$BIN/vita-make-fself" "$STAGE/eboot.velf" "$STAGE/eboot.bin"
NATIVE="$(python3 -c 'import json,sys; print("true" if json.load(open(sys.argv[1]))["native_launcher"] else "false")' "$WS/engine-state.json")"
APP_VER="$(python3 - "$ROOT/src/native_launcher/release.h" "$NATIVE" <<'PY2'
import re,sys
if sys.argv[2] == 'false':
    print('00.20') # The alternative RGSS launcher remains at 0.2 Alpha.
    raise SystemExit(0)
text=open(sys.argv[1]).read()
match=re.search(r'^#define HARDRPG_APP_VER "([0-9]{2}\.[0-9]{2})"$',text,re.M)
if not match: raise SystemExit('missing release app version')
print(match[1])
PY2
)"
"$BIN/vita-mksfoex" -s "TITLE_ID=$TITLE_ID" -s "APP_VER=$APP_VER" 'HardRPG' "$STAGE/param.sfo"
PICKER_ARGS=()
if [[ "$NATIVE" == true ]]; then
    PICKER_ARGS=(-a "$ROOT/launcher/native-backend.txt=launcher-backend.txt")
    FONT="$ROOT/assets/liberation.ttf"
else
    FONT="$ROOT/launcher/font.ttf"
    ruby "$ROOT/scripts/build-picker.rb" "$STAGE/stub"
    PICKER_ARGS=(-a stub/Game.ini=stub/Game.ini -a stub/Data/Scripts.rvdata2=stub/Data/Scripts.rvdata2)
fi

VPK="$OUT/HardRPG-$TITLE_ID-$PROFILE.vpk"
(cd "$STAGE" && "$BIN/vita-pack-vpk" \
    -s param.sfo -b eboot.bin \
    -a "$ROOT/launcher/livearea/icon0.png=sce_sys/icon0.png" \
    -a "$ROOT/launcher/livearea/pic0.png=sce_sys/pic0.png" \
    -a "$ROOT/launcher/livearea/bg0.png=sce_sys/livearea/contents/bg0.png" \
    -a "$ROOT/launcher/livearea/startup.png=sce_sys/livearea/contents/startup.png" \
    -a "$ROOT/launcher/livearea/template.xml=sce_sys/livearea/contents/template.xml" \
    -a "$ROOT/launcher/mkxp.json=mkxp.json" \
    -a "$ROOT/launcher/time_strftime_guard.rb=time_strftime_guard.rb" \
    -a "$ROOT/launcher/performance_patch.rb=performance_patch.rb" \
    -a "$FONT=font.ttf" \
    -a "$ROOT/launcher/assets/hardrpg-splash.png=stub/Graphics/Pictures/hardrpg-splash.png" \
    -a "$ROOT/THIRD-PARTY-NOTICES.md=THIRD-PARTY-NOTICES.md" \
    -a "$ROOT/COPYING=licenses/GPL-2.0.txt" \
    -a "$ROOT/LICENSES/GPL-3.0.txt=licenses/GPL-3.0.txt" \
    -a "$ROOT/LICENSES/LGPL-3.0.txt=licenses/LGPL-3.0.txt" \
    -a "$ROOT/LICENSES/Liberation.txt=licenses/Liberation.txt" \
    -a "$ROOT/LICENSES/math-neon.txt=licenses/math-neon.txt" \
    -a "$ROOT/src/display/libnsgif/COPYING=licenses/libnsgif.txt" \
    "${PICKER_ARGS[@]}" \
    "$VPK")
python3 "$ROOT/scripts/verify-launcher-payload.py" "$VPK" "$TITLE_ID"
sha256sum "$VPK"
