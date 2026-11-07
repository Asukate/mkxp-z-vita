# Pocket Mirror memory reproduction

Issue [#19](https://github.com/Asukate/mkxp-z-vita/issues/19) reports a long-session
failure in the original VX Ace edition. Physical-Vita tests were run on
2026-11-04 with the English game data and an isolated save copy.

## Reproduction boundary

The reported picture is `Graphics/Pictures/[FIXED] sombra corridorrrr`.
Map 17, Corridor, loads it from unconditional parallel event 5, alongside two
other map-sized light pictures. The local image inventory records a 2000×2000
PNG of 16,004,573 bytes. The reporter's game-file hashes were not available.

The accelerated sequence is:

```
6 → 17 → 12 → 16 → 17 → 53 → 17
```

A menu opens and closes on each visit. The save is loaded once, and each room
uses the game's real scene, picture, animation and audio code. Transfers use
test shortcuts and erase the previous room's picture names. They do not play
through story events. The runner does not clear the image cache or force GC
between visits. Its game/save/settings outputs have separate test paths.

The exact failure was observed on released 0.4.9 twice, at about 80 and 73 s,
and on released 0.5.1 at about 64 s:

```
SDLError: Error loading image 'Graphics/Pictures/[FIXED] sombra corridorrrr':
out of memory reading image
```

Sleep was not required for this reproduction. These results establish an
allocation failure under this workload; they do not establish a general leak
or exclude additional suspend/resume problems.

## Input allocation change

Previously, the bitmap reader loaded the complete encoded file into a C++
vector. SDL then allocated and copied the complete file again before PNG
decoding. Both copies overlapped the decoded pixel allocation.

The Vita bitmap reader now passes the file/archive stream to SDL directly.
The PNG decoder feeds libpng from that stream in bounded reads. JPEG retains
its existing decoder and needs one encoded input copy. Pixel conversion,
bitmap dimensions and RGSS bitmap behavior are unchanged.

For the corridor PNG this removes about 30.5 MiB of transient encoded input
storage. This is an allocation-path saving, not a claim that total process
memory always decreases by that amount. The failed 0.4.9 retry had about
47 MiB of total free C heap; total free space does not describe its largest
allocatable block. The change avoids requiring that large contiguous input
allocation.

`tests/image-streaming.py` runs the actual bitmap read helper and SDL decoder
with a synthetic 2000×2000 PNG. It denies whole-file-sized allocations while
allowing output pixels, verifies the pixel bytes, checks bounded read sizes,
and exercises truncation and both stream-ownership modes under ASan/UBSan.
The prior path fails with the reported input-allocation error. The streaming
path passes. Decoder allocation failures, JPEG colors and bitmap recovery
remain covered by the other image/memory checks in `ci/run.sh`.

## Physical validation

The matching focused sequence is used for baseline/candidate comparison.
Run hashes, full error reports and checkpoint telemetry are retained in the
local dated evidence bundle. Hardware results must be recorded separately
from host checks and cross-builds.

The streaming candidate completed 56 visits (eight complete cycles), including
24 corridor entries and 56 menu cycles, in 397 s without a memory error. A
released-0.5.1 repeat also completed this route in 412 s, while an earlier
released run failed in the corridor at 64 s. This is evidence for an
intermittent baseline, not a measured failure-rate guarantee for the candidate.

In the two completed runs, the largest sampled C-heap reservation was
107.43 MiB for the release and 102.57 MiB for the candidate. At the final
matching checkpoint it was 107.43 MiB and 92.08 MiB respectively. These are
sampled allocator reservations, not measurements of every transient peak.

## Map 63 failure before sparse storage

Before sparse storage, extending the sequence through Map 63 reached another failure:

```
MKXPError: Graphics memory: out of memory allocating 2480x3508 texture
```

This was observed on released 0.5.1, with the streaming change, and when
entering Map 63 from a fresh released process. It is a separate graphics
allocation boundary. Removing encoded input copies does not reduce the
storage of live large textures and does not establish full-game stability.

## Running the diagnostic fixture

`tests/fixtures/pocket-mirror-memory.rb` is a postload script, packaged as
`app0:/issue19-memory.rb`. It reads a plain Ruby Marshal configuration from
`app0:/issue19-config.marshal` and a copied save from
`app0:/issue19-save.rvdata2`. It supports `survey`, `cold` and `route` modes.

An example configuration before Marshal encoding is:

```json
{
  "run_id": "pocket-mirror-memory-example",
  "mode": "route",
  "output": "ux0:/data/blacksouls-vita/pocket-mirror-memory-example",
  "maps": [6, 17, 12, 16, 17, 53, 17],
  "cycles": 8,
  "spawns": {
    "6": [30, 17, 2],
    "17": [45, 61, 8],
    "12": [7, 13, 2],
    "16": [7, 11, 6],
    "53": [28, 47, 4]
  }
}
```

Create the output parent through the device harness before launch. Use a fresh
run output path, direct `gameFolder` boot, `execName: "Pocket Mirror"`,
`rgssVersion: 3`, and `postloadScript: ["app0:/issue19-memory.rb"]`.
Keep the release preloads. Assign a run-specific `vitaConfigPath` ending in a
unique 16-hex-digit `.json` basename so native crash-rescue output is isolated.
Use the same crash-rescue setting on both versions.

The fixture writes a flushed progress log and a terminal `result.marshal`.
An existing result is retained instead of starting over; this also prevents
older engines' automatic direct-game relaunch from hiding failures. A
90-second runner watchdog distinguishes a stalled test route from a completed
run. A passing bounded route is not a full-game compatibility result.

Record the reserved test Title ID, package hash, PC evidence destination and
exact cleanup set before installation. Use the canonical authenticated device
harness for install, launch, status, collection and close. Collect the progress
and result files, full native error report when present, screenshots and any
new dumps. Verify collection before removing the named temporary outputs.

## Map 63 follow-up: sparse image storage

Map 63 (Closed Doors / Harps 1st floor corridor NIGHT) aborts the game session
through the graphics-memory error handler on the release. The Vita remains
responsive. The fresh-process failure shows that this does not require hours
of accumulated memory use.

Several 2480×3508 pictures have a small nonconstant rectangle surrounded by
a constant transparent RGBA value. A full RGBA texture costs about 33.2 MiB
even when the visible chandelier occupies only 224×260 pixels.

The Vita image loader now detects large images with such a surround and
stores a padded rectangle when it saves at least a quarter of the texture
pixels.
Every RGBA difference is retained, including RGB beneath zero alpha. Rows
are packed in-place in the decoder buffer, without another full-image copy.
Logical bitmap dimensions and picture coordinates are preserved. Sprite
nearest/bilinear sampling uses the rectangle's texture origin; pattern and
bush effects keep their original source coordinates.

Direct render-target access, other texture consumers and wider filters
restore a full RGBA target first, with bounded row staging. Texture reuse
also retains the logical image metadata. This is a general image-storage
rule; game assets and scripts are not patched. Writable full-image operations
still require enough memory for the requested full render target.

Large-image single-pixel reads use mapped texture storage instead of
allocating a full RGBA readback. Synchronization occurs once per content
version; small windowskin readback caching retains its existing behavior.

Large ordinary bitmap copies also use mapped texture rows instead of a GPU
blit, without a complete staging buffer or render pass. Unmodified sparse
copies retain their crop metadata; small copies keep the prior GPU path.
A multi-image hardware probe previously returned a zero copied pixel while
the edited source pixel remained correct, motivating this change.

ASan/UBSan checks cover exact reconstruction, padded rows, hidden RGB and
single-pixel reads. Mesa comparisons cover plain/effect shaders with 60
combinations of crop boundaries, filtering and sprite effects. The canonical
Vita CI build passes.

Two final physical-Vita runs each completed eight Map 63/corridor round trips
with menus (16 visits and 16 menu cycles per run). One also performed exact
pixel/dimension, sparse clone, outside-crop write, edited clone and deferred
clear checks on four original large images before the route. Both runs
completed without an engine memory error; the independent fresh run used
the same route as an earlier candidate that failed after two Map 63 visits.
These bounded passes do not establish a full-game failure-rate guarantee.

The final storage rule requires at least 25% pixel savings. An earlier 50%
cutoff passed fresh entry but failed a repeated transfer: the parallax plugin
reloaded a previously evicted old background just to dispose it. The initial
Map 63 target-memory sample was unchanged by the lower cutoff, so no measured
16 MiB whole-scene reduction is claimed for that cutoff alone. Allocation
layout, image reuse and later loads also matter.

## Map 43 follow-up

Map 43 uses several 3776×3300 images. Each full RGBA image takes 47.5 MiB.
The sparse-storage build entered the room from a fresh process, then failed
on the first menu return in the repeated route. The engine was reloading
`[FIXED] fire light`. At the failed texture allocation, about 22 MiB of
system memory was free. After SDL freed the decoded image, about 69 MiB was
free. The difference identifies the overlap between decoding and uploading.

When graphics allocation fails, vitaGL can take ownership of a compatible
decoder buffer and use it directly as texture storage. This avoids a second
full-size allocation. The handoff checks buffer size, format and alignment.
The GPU's deferred release queue frees the buffer through its original
allocator after the GPU has finished using it.

The broader route also exhausted memory while decoding `[FIXED] pumpkin king`.
Most of this picture's 3776×3300 canvas is a constant transparent color.
If full pixel allocation fails, the bitmap loader can scan the PNG one row
at a time, then decode again and keep only the padded rectangle. The bitmap
keeps its original dimensions and RGBA values. This fallback supports
noninterlaced PNGs up to 4096×4096.

A focused repeat then ran out of memory decoding the fire-light picture.
Its padded rectangle saves about 9% of the full RGBA buffer. After a
full allocation fails, the decoder now tries any smaller padded rectangle.
Routine cropping still requires a 25% saving. A streamed rectangle can
also become the texture storage directly if its allocation and layout fit.

The smaller crop and a single graphics RAM region each still hit a
fire-light allocation failure on the mixed route. The synchronous PNG
loader now retries a large allocation through vitaGL's existing recovery
path after the ordinary pools fail. That path finishes the current GPU
scene and collects deferred storage. Background decoders continue using
the regular allocator.

The image probe also exposed an existing clear failure on a large bitmap.
Large transparent clears now synchronize with the GPU and zero the mapped
pixels directly. For a sparse image, the clear also zeroes the stored
background color. This avoids a full-size temporary target.

Host regressions deny a second upload allocation and a full PNG decode
allocation, then check ownership, cleanup, and exact pixels. The first
streamed-crop build completed 30 physical Vita visits and 30 menu cycles
in about 350 seconds,
including two visits each to Maps 43 and 63. The log recorded the streamed
crop fallback. The three original Map 43 image probes passed dimension,
pixel, clone, edit and clear checks, including a GPU blit after clear.
The pumpkin picture also passed size and pixel checks in the running room.

The modest-crop candidate completed the matching focused route on physical
Vita in 166 seconds. The final recovery build completed the 15-visit mixed
route and eight additional Map 43 and corridor round trips in one session.
It passed all 31 visits and menu cycles in 309 seconds. Both Map 43 pictures
passed size and pixel checks on all nine visits and their menu returns.
