#!/usr/bin/env bash
# Manual ruby header install mirroring the proven 191-file layout
# (deps/ruby-vita-headers.txt). make install-* fights cross mode (circular
# do-install-arch dep, target program links); every header here is either
# tracked source or configure-generated in-tree, installed by relative path.
set -euo pipefail
SRC="$1"; PREFIX="$2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST="$SCRIPT_DIR/../deps/ruby-vita-headers.txt"
DEST="$PREFIX/include/ruby-3.1.0"
count=0
while IFS= read -r rel; do
    [[ -n "$rel" ]] || continue
    found=""
    # Generated arch config lands at .ext/<arch>/ruby/config.h in-tree.
    if [[ "$rel" == "ruby/config.h" && -f "$SRC/.ext/include/arm-eabi/ruby/config.h" ]]; then
        found="$SRC/.ext/include/arm-eabi/ruby/config.h"
    fi
    if [[ -z "$found" ]]; then
        for base in "$SRC/include/ruby-3.1.0" "$SRC/include" "$SRC/.ext/include/arm-eabi"; do
            if [[ -f "$base/$rel" ]]; then found="$base/$rel"; break; fi
        done
    fi
    if [[ -z "$found" ]]; then printf 'ERROR: header has no in-tree source: %s\n' "$rel" >&2; exit 1; fi
    install -Dm644 "$found" "$DEST/$rel"
    count=$((count+1))
done < "$MANIFEST"
printf 'headers: %d files -> %s\n' "$count" "$DEST"
