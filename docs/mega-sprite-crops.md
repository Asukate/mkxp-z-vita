# Sprites using oversized bitmap sheets

File bitmaps larger than the graphics texture limit remain CPU-backed mega
surfaces. A Sprite can reference one and then select a smaller `src_rect`.
The original bitmap identity and dimensions are retained for scripts and for
other sprites using the same sheet.

Before drawing a visible sprite, the runtime copies its clamped source region
to an owned GPU bitmap. It includes up to three neighboring pixels per edge
for smooth filtering, subject to the texture limit. An unchanged selection
reuses that bitmap. Source modification invalidates the cache; reassignment,
source disposal, and sprite disposal release it. Empty and invisible selections
do not allocate a crop.

Texture coordinates refer to the cached region. Bush and pattern coordinates
continue to refer to the original sheet, preserving the effects when an icon
or character frame is selected far down a large image. Mirror, origin, zoom,
rotation, and wave geometry stay in sprite space.

The selected region must fit within the texture limit in each dimension. A
visible selection larger than that limit raises a specific error at draw
preparation. Rendering a whole oversized picture would require tiled drawing;
this crop path does not implement that. At the maximum selection size, filter
padding may be reduced to stay within the graphics limit. Oversized Plane,
windowskin, and pattern bitmaps retain their existing restrictions.

Run `python3 tests/sprite-mega.py` on a host with a C++ compiler, SDL2, GLEW,
Mesa software OpenGL, and `xvfb-run`. The test compiles the production sprite
assignment and preparation code with a CPU bitmap stand-in, uses the actual
transform and signal code, and compares pixels from the production effect
shader against a full GPU sheet. Address and undefined-behavior sanitizers
check the fixture; internal bitmap counters check crop ownership. It also
checks source changes, shared disposal, cache reuse, clipping, allocation/blit
failure rollback, and ordinary sprites. `--baseline PATH` additionally compares
with that checkout's effect shader. `--source PATH --assignment-only` tests the
assignment-then-crop sequence against a different runtime revision.

Host checks and Vita cross-compilation do not establish physical Vita rendering
or gameplay acceptance.
