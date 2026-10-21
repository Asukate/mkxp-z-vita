#!/usr/bin/env python3
"""Write the launcher UI icons as standalone 24x24 SVG masters (white, tinted at runtime)."""
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent

out = Path(sys.argv[1] if len(sys.argv) > 1 else root / 'icons')
out.mkdir(parents=True, exist_ok=True)
W = '#ffffff'
S = f'fill="none" stroke="{W}" stroke-linecap="round" stroke-linejoin="round"'

gear_teeth = ''.join(f'<rect x="10.2" y="1.6" width="3.6" height="5" rx="1" transform="rotate({a} 12 12)"/>' for a in range(0, 360, 45))
ICONS = {
    'gear': f'<defs><mask id="m"><rect width="24" height="24" fill="#fff"/><circle cx="12" cy="12" r="3.4" fill="#000"/></mask></defs>'
            f'<g fill="{W}" mask="url(#m)"><circle cx="12" cy="12" r="7"/>{gear_teeth}</g>',
    'info': f'<circle cx="12" cy="12" r="9.5" {S} stroke-width="1.8"/><rect x="10.8" y="10.2" width="2.4" height="7" rx="1.2" fill="{W}"/><circle cx="12" cy="7" r="1.45" fill="{W}"/>',
    'search': f'<circle cx="10.2" cy="10.2" r="7" {S} stroke-width="2.1"/><path d="M15.6 15.6l6 6" {S} stroke-width="2.4"/>',
    'filter': f'<path d="M2.5 5h19M6 12h12M9.5 19h5" {S} stroke-width="2.4"/>',
    'view-carousel': f'<rect x="7.2" y="3.5" width="9.6" height="17" rx="1.8" fill="{W}"/><rect x="1.2" y="6" width="4.2" height="12" rx="1.2" fill="{W}" opacity=".55"/><rect x="18.6" y="6" width="4.2" height="12" rx="1.2" fill="{W}" opacity=".55"/>',
    'view-grid': ''.join(f'<rect x="{x}" y="{y}" width="8.4" height="8.4" rx="1.8" fill="{W}"/>' for y in (2.4, 13.2) for x in (2.4, 13.2)),
    'plus': f'<path d="M12 3.5v17M3.5 12h17" {S} stroke-width="2.6"/>',
    'folder': f'<path d="M3 6.5A1.5 1.5 0 014.5 5h4.6l2 2.2h8.4A1.5 1.5 0 0121 8.7v9.8a1.5 1.5 0 01-1.5 1.5h-15A1.5 1.5 0 013 18.5z" {S} stroke-width="1.7"/>',
    'memcard': f'<path d="M6.5 2.5h8l4.5 4.5v13.5a1 1 0 01-1 1h-11.5a1 1 0 01-1-1v-17a1 1 0 011-1z" {S} stroke-width="1.7"/><path d="M9.5 6v3.5M12.5 6v3.5" {S} stroke-width="1.6"/>',
    'star': f'<path d="M12 2.6l2.75 6.05 6.6.66-4.95 4.4 1.43 6.5L12 16.85l-5.83 3.36 1.43-6.5-4.95-4.4 6.6-.66z" fill="{W}"/>',
    'star-outline': f'<path d="M12 2.6l2.75 6.05 6.6.66-4.95 4.4 1.43 6.5L12 16.85l-5.83 3.36 1.43-6.5-4.95-4.4 6.6-.66z" {S} stroke-width="1.6"/>',
    'sort-recent': f'<circle cx="12" cy="12" r="9" {S} stroke-width="1.8"/><path d="M12 6.6V12l3.6 2.4" {S} stroke-width="1.8"/>',
    'sort-name': f'<path d="M3 11l3-8 3 8M4.1 8.2h3.8M3.2 13.5h5.6L3.2 21h5.6" {S} stroke-width="1.7"/><path d="M17 3.5v16M13.5 16l3.5 3.5 3.5-3.5" {S} stroke-width="1.8"/>',
    'sort-time': f'<path d="M5.5 2.8h13M5.5 21.2h13M7.2 2.8h9.6L12 12l4.8 9.2H7.2L12 12z" {S} stroke-width="1.7"/><path d="M9.6 19.6h4.8L12 16.4z" fill="{W}"/>',
    'sort-size': f'<path d="M6.5 2.5h8l4.5 4.5v13.5a1 1 0 01-1 1h-11.5a1 1 0 01-1-1v-17a1 1 0 011-1z" {S} stroke-width="1.7"/><path d="M9.5 6v3.5M12.5 6v3.5" {S} stroke-width="1.6"/>',
    'sort-added': f'<rect x="3.5" y="3.5" width="17" height="17" rx="3.5" {S} stroke-width="1.8"/><path d="M12 8v8M8 12h8" {S} stroke-width="1.8"/>',
    'arrow-down': f'<path d="M12 4v15M6 13.5l6 6 6-6" {S} stroke-width="2.2"/>',
    'arrow-up': f'<path d="M12 20V5M6 10.5l6-6 6 6" {S} stroke-width="2.2"/>',
    # Vita face buttons: plain symbol on a dark disc, no PlayStation colours.
    'btn-cross': f'<circle cx="12" cy="12" r="11" fill="#000" fill-opacity=".5" stroke="{W}" stroke-opacity=".45"/><path d="M8.2 8.2l7.6 7.6M15.8 8.2l-7.6 7.6" {S} stroke-width="2"/>',
    'btn-circle': f'<circle cx="12" cy="12" r="11" fill="#000" fill-opacity=".5" stroke="{W}" stroke-opacity=".45"/><circle cx="12" cy="12" r="5" {S} stroke-width="1.9"/>',
    'btn-triangle': f'<circle cx="12" cy="12" r="11" fill="#000" fill-opacity=".5" stroke="{W}" stroke-opacity=".45"/><path d="M12 7.2l5 8.7H7z" {S} stroke-width="1.8"/>',
    'btn-square': f'<circle cx="12" cy="12" r="11" fill="#000" fill-opacity=".5" stroke="{W}" stroke-opacity=".45"/><rect x="7.6" y="7.6" width="8.8" height="8.8" {S} stroke-width="1.8"/>',
    'battery': f'<rect x="1" y="6" width="19.5" height="12" rx="2.6" {S} stroke-width="1.5"/><rect x="21.3" y="9.6" width="1.8" height="4.8" rx=".8" fill="{W}"/>',
}
for name, body in ICONS.items():
    (out / f'{name}.svg').write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24">{body}</svg>\n')
print(len(ICONS), 'icons ->', out)
