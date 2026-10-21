# Launcher artwork sources

The launcher draws PNGs from `launcher/ui/`. This folder holds the vector
masters they are rendered from, and the scripts that made the masters.

| Files | What they are |
| --- | --- |
| `icons/*.svg` | UI icons, 24×24, white (tinted when drawn). Written by `tools/make_icons.py`. Project-authored. |
| `emblem.svg`, `emblem-small.svg` | The ruby emblem, traced from `launcher/assets/hardrpg-splash.png` by `tools/make_emblem.py` (needs potrace). The small version is a two-tone cut for 24–48 px. |
| `wordmark*.svg` | "HardRPG" built from Russo One outlines (SIL OFL 1.1, `fonts/`, licence in `LICENSES/RussoOne.txt`), slanted to match the startup logo. Written by `tools/make_wordmark.py` (needs fontTools). |

`tools/render.sh` renders the masters to the exact files and sizes in
`launcher/ui/` (needs `rsvg-convert`). Running it on unchanged masters gives
identical images.

`launcher/ui/vita-640.png` is not made here: it is Tokyoship's illustration
from Wikimedia Commons (CC BY 3.0), see `LICENSES/Vita-illustration.txt`.

## Sounds

`launcher/sfx/*.ogg` are the launcher's UI sounds (move, select, back, open,
close, toggle, error, launch). They are project-authored: `tools/make_sfx.py`
synthesizes them from scratch (FM bells, filtered noise and a small room
reverb) and encodes them with ffmpeg's libvorbis. It needs numpy, scipy and
ffmpeg. A rerun gives byte-identical files.
