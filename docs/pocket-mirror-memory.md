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

## Separate graphics working-set limit

Extending the sequence through Map 63 reaches another failure:

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
