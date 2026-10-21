#!/usr/bin/env python3
"""Build the HardRPG wordmark SVGs from Russo One outlines (OFL), slanted like the startup logo."""
import sys
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

root = Path(__file__).resolve().parent.parent
font = TTFont(root / 'fonts/RussoOne-Regular.ttf')
out = Path(sys.argv[1] if len(sys.argv) > 1 else root)
cmap, glyphs, hmtx = font.getBestCmap(), font.getGlyphSet(), font['hmtx']
upm = font['head'].unitsPerEm
SLANT = 0.12          # tan of the lean in the startup logo (about 7 degrees)
STRETCH = 0.97        # Russo One set slightly narrower to match the startup logo
TRACK = -24           # font units between letters
XH = 1.1              # the original's lowercase stands taller
BOLD = 36             # extra stroke weight in font units

def run(text, x0):
    """Return (svg path data, end x) for text, with y flipped and slanted."""
    pen, x = SVGPathPen(glyphs), x0
    for ch in text:
        name = cmap[ord(ch)]
        # x' = (x + slant*y) * stretch, y' = -y : flips to SVG space and leans the letters right
        sy = XH if ch.islower() else 1
        tp = TransformPen(pen, (STRETCH, 0, SLANT * STRETCH * sy, -sy, x * STRETCH, 0))
        glyphs[name].draw(tp)
        x += hmtx[name][0] + TRACK
    return pen.getCommands(), x

cap = font['OS/2'].sCapHeight
hard, x = run('Hard', 0)
rpg, x = run('RPG', x + 10)
width = x * STRETCH + SLANT * cap
pad = 24
vb = f'{-pad} {-cap - pad} {width + 2 * pad:.0f} {cap + 2 * pad}'

def svg(fill_hard, fill_rpg, defs=''):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}">{defs}'
            f'<path d="{hard}" fill="{fill_hard}" stroke="{fill_hard}" stroke-width="{BOLD}" stroke-linejoin="round"/>'
            f'<path d="{rpg}" fill="{fill_rpg}" stroke="{fill_rpg}" stroke-width="{BOLD}" stroke-linejoin="round"/></svg>\n')

grad = lambda gid, top, bottom: (f'<linearGradient id="{gid}" x1="0" y1="{-cap}" x2="0" y2="0" gradientUnits="userSpaceOnUse">'
                                 f'<stop offset="0" stop-color="{top}"/><stop offset="1" stop-color="{bottom}"/></linearGradient>')
red = grad('r', '#ff3030', '#b4000e')
variants = {
    # On the dark launcher UI: light steel "Hard", ruby "RPG".
    'wordmark.svg': svg('url(#h)', 'url(#r)', f'<defs>{grad("h", "#f2f2f4", "#a9a9b0")}{red}</defs>'),
    # The original LiveArea look: dark steel "Hard" for light or mid backgrounds.
    'wordmark-dark.svg': svg('url(#h)', 'url(#r)', f'<defs>{grad("h", "#77777d", "#2a2a2e")}{red}</defs>'),
    # Flat colours for small sizes (no gradients to smear).
    'wordmark-flat.svg': svg('#ececef', '#e3132a'),
    'wordmark-white.svg': svg('#ffffff', '#ffffff'),
}
for name, data in variants.items():
    (out / name).write_text(data)
print('wordmark', vb, '->', out)
