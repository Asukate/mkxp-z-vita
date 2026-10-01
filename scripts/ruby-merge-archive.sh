#!/usr/bin/env bash
# Deterministic libruby-3.1.a merge for Vita cross builds.
#
# Upstream `make` only archives core OBJS (with dmyext/dmyenc placeholders);
# enc/trans/ext objects live in their own trees. Merging them into one archive
# needs collision-avoiding member names (enc/big5.o and enc/trans/big5.o would
# otherwise collide as `big5.o`). This script reproduces the proven member
# layout exactly (see deps/ruby-vita-archive-members.txt):
#   core root objects as-is (dmy placeholders excluded, real
#     encinit.o/extinit.o used instead)
#   enc/<x>.o          -> enc_<x>.o
#   enc/trans/<x>.o    -> trans_<x>.o
#   ext/<p>/<x>.o      -> ext_<p with /->_>_<x>.o, except ext/zlib/zlib.o
#                        which stays zlib.o (proven layout)
# thread_pthread.o is deliberately omitted: the reference copy is an empty
# 724-byte object with no symbols (verified with nm), hence inert.
set -euo pipefail
SRC="$1"; PREFIX="$2"; VITABIN="$3"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST="$SCRIPT_DIR/../deps/ruby-vita-archive-members.txt"
STAGE="$SRC/ruby-merge-stage"
rm -rf "$STAGE"; mkdir -p "$STAGE"
map_source() { # $1 = member -> prints source path or SKIP/FAIL
    local m="$1"
    if [[ "$m" == "thread_pthread.o" ]]; then echo "SKIP-inert-empty"; return; fi
    if [[ "$m" == encinit.o ]]; then echo "enc/encinit.o"; return; fi
    if [[ "$m" == extinit.o ]]; then echo "ext/extinit.o"; return; fi
    if [[ "$m" == zlib.o ]]; then echo "ext/zlib/zlib.o"; return; fi
    case "$m" in
        ascii.o|unicode.o|us_ascii.o|utf_8.o) echo "enc/$m"; return ;;
        newline.o) echo "enc/trans/newline.o"; return ;;
    esac
    if [[ "$m" == enc_*.o ]]; then echo "enc/${m#enc_}"; return; fi
    if [[ "$m" == trans_*.o ]]; then echo "enc/trans/${m#trans_}"; return; fi
    if [[ "$m" == ext_cgi_escape.o ]]; then echo "ext/cgi/escape/escape.o"; return; fi
    if [[ "$m" == ext_continuation_continuation.o ]]; then echo "ext/continuation/continuation.o"; return; fi
    if [[ "$m" == ext_coverage_coverage.o ]]; then echo "ext/coverage/coverage.o"; return; fi
    if [[ "$m" == ext_date_date_*.o ]]; then echo "ext/date/date_${m#ext_date_date_}"; return; fi
    if [[ "$m" == ext_digest_digest.o ]]; then echo "ext/digest/digest.o"; return; fi
    echo "$m"
}
# Basename index: make's archive rule stores ar basenames, so a member like
# Context.o may live in a subdir (e.g. coroutine/). Unprefixed members
# resolve via the index (exactly one hit required); prefixed members use
# explicit collision-avoiding rules.
declare -A OINDEX
while IFS= read -r obj; do
    base="${obj##*/}"
    if [[ -n "${OINDEX[$base]:-}" ]]; then OINDEX[$base]="${OINDEX[$base]}|$obj";
    else OINDEX[$base]="$obj"; fi
done < <(cd "$SRC" && find . -name '*.o' -not -path './ruby-merge-stage/*')
count=0
while IFS= read -r member; do
    [[ -n "$member" ]] || continue
    src_rel="$(map_source "$member")"
    if [[ "$src_rel" == "$member" ]]; then
        hits="${OINDEX[$member]:-}"
        if [[ -z "$hits" ]]; then printf 'ERROR: no object builds member %s\n' "$member" >&2; exit 1; fi
        if [[ "$hits" == *"|"* ]]; then printf 'ERROR: ambiguous member %s: %s\n' "$member" "$hits" >&2; exit 1; fi
        src_rel="${hits#./}"
    fi
    if [[ "$src_rel" == SKIP* ]]; then printf 'merge: skipping %s (%s)\n' "$member" "$src_rel"; continue; fi
    if [[ ! -f "$SRC/$src_rel" ]]; then printf 'ERROR: merge source missing: %s (member %s)\n' "$src_rel" "$member" >&2; exit 1; fi
    if [[ "$member" == "dmyext.o" || "$member" == "dmyenc.o" ]]; then printf 'ERROR: dummy placeholder in manifest: %s\n' "$member" >&2; exit 1; fi
    cp "$SRC/$src_rel" "$STAGE/$member"
    count=$((count+1))
done < "$MANIFEST"
"$VITABIN/arm-vita-eabi-ar" rcs "$PREFIX/lib/libruby-3.1.a" "$STAGE"/*.o
"$VITABIN/arm-vita-eabi-ranlib" "$PREFIX/lib/libruby-3.1.a"
printf 'merge: %d members -> %s\n' "$count" "$PREFIX/lib/libruby-3.1.a"
