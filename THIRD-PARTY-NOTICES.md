# HardRPG third-party notices

mkxp-z-vita ports mkxp-z to PS Vita and includes the HardRPG launcher. Each
component retains its own notices and terms; original copyright headers remain
in source. The source paths below refer to the matching source distribution.

| Component | Source and terms |
| --- | --- |
| mkxp-z / mkxp engine | Based on mkxp-z `1f412ce`; GPL-2.0-or-later, [COPYING](COPYING). |
| Vita platform additions | See individual file headers. Project-authored SDL shim and dependency adapters specify GPL v3; full text: [GPL-3.0](LICENSES/GPL-3.0.txt). |
| libcurl 8.17.0 | Launcher release checks, curl license: [COPYING](LICENSES/curl.txt). Pinned source archive and Vita build recipe in `scripts/bootstrap-release-network.sh`. |
| Mbed TLS 3.6.5 | VitaSDK library used by the launcher HTTPS client; GPL-2.0-or-later choice of its dual license. Copyright The Mbed TLS Contributors. Source: https://github.com/Mbed-TLS/mbedtls/tree/mbedtls-3.6.5, [GPL text](COPYING). |
| Mozilla CA certificate bundle | Mozilla certificate data, extracted by curl on 2026-09-25; MPL-2.0 [license](LICENSES/MPL-2.0.txt). Bundle: `assets/ca-certificates.crt`, SHA-256 a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505. Source/extraction: https://curl.se/docs/caextract.html. |
| FluidLite 1.2.2 | Native Vita MIDI synthesis; Robin Lobel and FluidSynth authors. LGPL-2.1-or-later, [notice](LICENSES/FluidLite.txt), with [LGPL-3.0](LICENSES/LGPL-3.0.txt) supplied for the later-version option. VitaSDK source commit `4a01cf1c67419e71da971d209f2855bbf4f3bab8`: https://github.com/divideconcept/FluidLite/tree/4a01cf1c67419e71da971d209f2855bbf4f3bab8. |
| TimGM6mb SoundFont | Default General MIDI bank, Copyright Tim Brechbill and David Bolton, GPL-2.0-only. [Notice and source hashes](LICENSES/TimGM6mb.txt). Unmodified editable SoundFont source included in `assets/TimGM6mb.sf2`. |
| Ruby 3.1 | Bundled source snapshot under `deps/vendor/`; Ruby license and alternative BSD terms in its `COPYING` and `BSDL`. The Vita patch is included in `deps/`. |
| SDL-derived headers | SDL 2.30.0, zlib license: [LICENSE-SDL.txt](deps/vendor/vita-sdl2-shim/LICENSE-SDL.txt). |
| vitaGL and vitaShaRK | Fetched from Rinnegatamante's public repositories at locked revisions; LGPL v3. [LGPL-3.0](LICENSES/LGPL-3.0.txt). Local vitaGL patch is included. |
| math-neon | MIT, Lachlan Tychsen-Smith; [license](LICENSES/math-neon.txt). |
| libnsgif | MIT, NetSurf contributors; [COPYING](src/display/libnsgif/COPYING). |
| stb_image | Dual public-domain / MIT terms in its header. |
| dr_mp3 | MIT-0 terms in `deps/dr_libs/dr_mp3.h`. |
| Liberation fonts | SIL Open Font License 1.1, including the packaged `launcher/font.ttf`; [license](LICENSES/Liberation.txt). |
| Rajdhani | Launcher title font `launcher/font-display.ttf`, Indian Type Foundry; SIL Open Font License 1.1, [license](LICENSES/Rajdhani.txt). |
| Russo One | Its letter outlines form the HardRPG wordmark (`launcher/art/wordmark*.svg`, rendered to `launcher/ui/wordmark-*.png`); Jovanny Lemonad, SIL Open Font License 1.1, [license](LICENSES/RussoOne.txt). The font file is kept only as a build source in `launcher/art/fonts/` and is not packaged. |
| PlayStation Vita illustration | `launcher/ui/vita-640.png`, Tokyoship, Wikimedia Commons, CC BY 3.0, modified; [notice](LICENSES/Vita-illustration.txt). |
| Launcher icons and emblem | Project-authored. SVG masters and the scripts that make them are in `launcher/art/`; the emblem is traced from HardRPG's own splash image. |
| WenQuanYi Micro Hei | Optional CJK fallback; GPL v3 with font embedding exception / Apache 2.0. See [font notices](LICENSES/WenQuanYi.txt). |
| EasyRPG Player backend | Optional native-launcher backend, Player 0.8.1.1 (`78328fa29f465315291e161130e6682f69410370`), GPL-3.0-or-later. HardRPG entry point and dated patch: `backends/easyrpg/`, `deps/patches/easyrpg/`. Source: https://github.com/EasyRPG/Player. |
| liblcf | 0.8.1 (`92c4450a1bc1acb58bd02bbb99b57e5036919cdf`), MIT. Source: https://github.com/EasyRPG/liblcf. |
| EasyRPG embedded fonts and decoders | ttyp0 (ttyp0 license), Shinonome (public domain), Baekmuk (Baekmuk license), WenQuanYi (GPL-2.0-or-later with font exception), FmMidi (BSD-3-Clause), dr_wav (public domain / MIT-0). Exact notices are retained in matching Player sources and packaged under `licenses/easyrpg/`. |

The dependency lock is executable input to the bootstrap, not a list of
suggested versions: [deps/versions.lock](deps/versions.lock). Additional
libraries fetched at build time retain their own license files in workspace
source directories: zlib (zlib), bzip2 (bzip2 license), libpng (libpng license),
libogg/libvorbis/libtheora (BSD-style), pixman (MIT), FreeType (FTL or GPL),
PhysicsFS (zlib), uchardet (MPL-1.1 / GPL / LGPL), and libiconv (LGPL).

The VPK includes this document as `THIRD-PARTY-NOTICES.md` and the following
files under `licenses/`: `GPL-2.0.txt` (the engine's `COPYING`), `GPL-3.0.txt`,
`LGPL-3.0.txt`, `Liberation.txt`, `math-neon.txt`, `libnsgif.txt`, `curl.txt`, `MPL-2.0.txt`, `FluidLite.txt`, `TimGM6mb.txt`, `Rajdhani.txt` and `Vita-illustration.txt`. Other
component notices remain in their source files or dependency source trees.
The matching source revision, patches, dependency lock, and build recipes
must accompany binary distribution. The repository includes no commercial game
files, RTP,
saves, or Sony firmware modules. Obtain game files and device modules separately.

Packages containing `runtime/easyrpg.bin` also include its build manifest and
Player/liblcf notices under `licenses/easyrpg/`. Each executable retains its own
terms. Provide the corresponding source, integration patch and build recipes,
plus the matching source and modifications for linked dependencies. Dependency
archive hashes alone do not satisfy source requirements. EasyRPG logo and
touch-overlay artwork are not used by HardRPG. See [backend build and license
details](docs/easyrpg.md).
