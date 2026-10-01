# HardRPG third-party notices

mkxp-z-vita ports mkxp-z to PS Vita and includes the HardRPG launcher. Each
component retains its own notices and terms; original copyright headers remain
in source. The source paths below refer to the matching source distribution.

| Component | Source and terms |
| --- | --- |
| mkxp-z / mkxp engine | Based on mkxp-z `1f412ce`; GPL-2.0-or-later, [COPYING](COPYING). |
| Vita platform additions | See individual file headers. Project-authored SDL shim and dependency adapters specify GPL v3; full text: [GPL-3.0](LICENSES/GPL-3.0.txt). |
| Ruby 3.1 | Bundled source snapshot under `deps/vendor/`; Ruby license and alternative BSD terms in its `COPYING` and `BSDL`. The Vita patch is included in `deps/`. |
| SDL-derived headers | SDL 2.30.0, zlib license: [LICENSE-SDL.txt](deps/vendor/vita-sdl2-shim/LICENSE-SDL.txt). |
| vitaGL and vitaShaRK | Fetched from Rinnegatamante's public repositories at locked revisions; LGPL v3. [LGPL-3.0](LICENSES/LGPL-3.0.txt). Local vitaGL patch is included. |
| math-neon | MIT, Lachlan Tychsen-Smith; [license](LICENSES/math-neon.txt). |
| libnsgif | MIT, NetSurf contributors; [COPYING](src/display/libnsgif/COPYING). |
| stb_image | Dual public-domain / MIT terms in its header. |
| dr_mp3 | MIT-0 terms in `deps/dr_libs/dr_mp3.h`. |
| Liberation fonts | SIL Open Font License 1.1, including the packaged `launcher/font.ttf`; [license](LICENSES/Liberation.txt). |
| WenQuanYi Micro Hei | Optional CJK fallback; GPL v3 with font embedding exception / Apache 2.0. See [font notices](LICENSES/WenQuanYi.txt). |

The dependency lock is executable input to the bootstrap, not a list of
suggested versions: [deps/versions.lock](deps/versions.lock). Additional
libraries fetched at build time retain their own license files in workspace
source directories: zlib (zlib), bzip2 (bzip2 license), libpng (libpng license),
libogg/libvorbis/libtheora (BSD-style), pixman (MIT), FreeType (FTL or GPL),
PhysicsFS (zlib), uchardet (MPL-1.1 / GPL / LGPL), and libiconv (LGPL).

The VPK includes this document as `THIRD-PARTY-NOTICES.md` and the following
files under `licenses/`: `GPL-2.0.txt` (the engine's `COPYING`), `GPL-3.0.txt`,
`LGPL-3.0.txt`, `Liberation.txt`, `math-neon.txt`, and `libnsgif.txt`. Other
component notices remain in their source files or dependency source trees.
The matching source revision, patches, dependency lock, and build recipes
must accompany binary distribution. The repository includes no commercial game
files, RTP,
saves, or Sony firmware modules. Obtain game files and device modules separately.
