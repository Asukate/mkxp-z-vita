# Validation

HardRPG 0.1 Alpha has been compiled and packaged with the pinned VitaSDK
container described in [BUILDING.md](BUILDING.md). Source syntax, payload
hashes, picker source, title, Title ID and app version are checked before a
VPK is accepted. Matching source and build receipts accompany the release.

Host integration checks cover game detection, nested folders, external
libraries, ZIP preparation and cache reuse, save preservation, cancellation,
path validation, display settings, and RTP folder/ZIP selection. Font checks
compare the production Vita rasterizer's coverage and layout with FreeType
and SDL_ttf. Bitmap checks exercise queued clear/fill ordering.

The desktop preview runs the production launcher scripts through an SDL
adapter. It verifies layout and navigation without installing a VPK, but
does not emulate Vita rendering, controller behavior or game performance.

The final HardRPG 0.1 Alpha release VPK was installed and tested on a real
PS Vita on October 1, 2026. The installed executable was read back and
matched to the release package. The launcher and all games listed in
[COMPATIBILITY.md](COMPATIBILITY.md) were retested. No new regressions were
reported; current performance, font, visual and crash limitations are
recorded in that table. The return shortcut was also previously verified
from the Black Souls II title.

These checks do not establish full-game compatibility, exact performance,
or hardware acceptance of every Settings/RTP combination. Individual game
results and remaining checks are listed in [COMPATIBILITY.md](COMPATIBILITY.md).
