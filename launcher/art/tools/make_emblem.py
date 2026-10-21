#!/usr/bin/env python3
"""Trace the ruby triskelion from the splash plate into a layered SVG (potrace, one layer per brightness band)."""
import re, subprocess, sys, tempfile
from pathlib import Path
from PIL import Image, ImageFilter

root = Path(__file__).resolve().parent.parent
SPLASH = root.parent / 'assets/hardrpg-splash.png'
out = Path(sys.argv[1] if len(sys.argv) > 1 else root / 'emblem.svg')
small = out.with_name('emblem-small.svg')
UP = 6  # trace at 6x so potrace sees smooth edges

src = Image.open(SPLASH).convert('RGB').crop((340, 120, 620, 395))
src = src.resize((src.width * UP, src.height * UP), Image.LANCZOS).filter(ImageFilter.GaussianBlur(UP * 0.6))
W, H = src.size
px = src.load()
red = Image.new('L', src.size)   # how red a pixel is (0 = not part of the emblem)
lum = Image.new('L', src.size)   # brightness inside the emblem
rp, lp = red.load(), lum.load()
for y in range(H):
    for x in range(W):
        r, g, b = px[x, y]
        rp[x, y] = max(0, min(255, (r - max(g, b) - 25) * 4))
        lp[x, y] = r

# Silhouette: red pixels plus any dark facet fully enclosed by them (flood the background in from the border).
sil = Image.new('L', src.size, 0); sp = sil.load()
for y in range(H):
    for x in range(W):
        if rp[x, y] > 70: sp[x, y] = 255
from PIL import ImageDraw
ImageDraw.floodfill(sil, (0, 0), 128)
for y in range(H):
    for x in range(W):
        sp[x, y] = 0 if sp[x, y] == 128 else 255
# (mask test, colour): base silhouette first, then brighter bands stacked on top
LAYERS = [
    (lambda x, y: sp[x, y] == 255, '#5e0611'),
    (lambda x, y: rp[x, y] > 110 and lp[x, y] > 120, '#a50d1f'),
    (lambda x, y: rp[x, y] > 110 and lp[x, y] > 170, '#d81a30'),
    (lambda x, y: rp[x, y] > 110 and lp[x, y] > 215, '#ff4a5e'),
    (lambda x, y: lp[x, y] > 245 and rp[x, y] > 40, '#ff9aa4'),
]
paths = []
with tempfile.TemporaryDirectory() as tmp:
    for i, (test, colour) in enumerate(LAYERS):
        m = Image.new('1', src.size, 1)
        mp = m.load()
        for y in range(H):
            for x in range(W):
                if test(x, y): mp[x, y] = 0
        pbm, svg = Path(tmp) / f'{i}.pbm', Path(tmp) / f'{i}.svg'
        m.save(pbm)
        subprocess.run(['potrace', str(pbm), '-s', '-o', str(svg), '-t', str(UP * UP * 6), '-a', '1.1', '-O', '0.6', '--flat'], check=True)
        text = svg.read_text()
        tf = re.search(r'<g transform="([^"]+)"', text).group(1)
        for d in re.findall(r' d="([^"]+)"', text):
            paths.append((i, f'<path transform="{tf}" fill="{colour}" d="{d}"/>'))
# potrace works in the 6x space; the viewBox maps it back to the 280x275 crop
head = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}">'
out.write_text(head + ''.join(p for _, p in paths) + '</svg>\n')
# Small sizes: silhouette plus one bright band reads better than five layers at 24-48 px
small.write_text(head + ''.join(p.replace('#d81a30', '#e8203a') for i, p in paths if i in (0, 2)) + '</svg>\n')
print(len(paths), 'paths ->', out, small)
