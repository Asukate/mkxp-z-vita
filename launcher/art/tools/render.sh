#!/bin/sh
# Render the SVG masters to the PNGs the launcher loads from launcher/ui/.
# Usage: launcher/art/tools/render.sh   (needs rsvg-convert)
set -e
art="$(cd "$(dirname "$0")/.." && pwd)"
ui="$art/../ui"
r() { rsvg-convert "$@"; }
r -w 40 "$art/emblem-small.svg" -o "$ui/emblem-40.png"
r -w 72 "$art/emblem.svg" -o "$ui/emblem-72.png"
r -h 25 "$art/wordmark.svg" -o "$ui/wordmark-25.png"
r -h 46 "$art/wordmark.svg" -o "$ui/wordmark-46.png"
for n in filter plus search view-carousel view-grid view-list; do r -w 31 -h 31 "$art/icons/$n.svg" -o "$ui/$n-31.png"; done
for n in gear info; do r -w 35 -h 35 "$art/icons/$n.svg" -o "$ui/$n-35.png"; done
for n in btn-circle btn-cross btn-square btn-triangle folder memcard; do r -w 26 -h 26 "$art/icons/$n.svg" -o "$ui/$n-26.png"; done
for n in sort-name sort-recent sort-time star; do r -w 24 -h 24 "$art/icons/$n.svg" -o "$ui/$n-24.png"; done
