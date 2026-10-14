# Tap-to-walk adapter

`launcher/vita_tap_to_walk.rb` provides opt-in movement for stock tile movement
in XP, VX and VX Ace. It is bundled with the app and turned on per game with
Triangle -> Controls -> Tap to walk. Once the game's `Game_Player` and
`Scene_Map` exist, the engine loads it and calls `HardRPGTapWalk.install!`,
which selects the XP, VX or Ace adapter from the player API (an explicit
`install!(:xp)`, `install!(:vx)` or `install!(:ace)` is also accepted). With tap
to walk on, the front screen supplies `Input.mouse_x/y` and
`Input.trigger?(:MOUSELEFT)`; this adapter translates a tap into a map tile.

The bounded A* search uses the player's normal collision checks. VX Ace moves
with `move_straight`; XP/VX use their ordinary directional movement methods.
XP's adapter wraps the player update, while VX/Ace wrap `move_by_input`.
It preserves ordinary movement, followers, step hooks, touch events and encounter
progression. XP and VX camera offsets are normalized from their 128/256-unit
tile coordinates; Ace uses tile units directly. It adds this camera offset to the runtime's logical screen
coordinates and supports looping maps. It assumes 32-pixel tiles and the stock
camera transform; custom zoom, pixel movement and custom map input need adapters.

A translucent white square expands from the tile centre and fades in a repeating
20-frame cycle while planning and walking. It uses additive blending and grows
from tile size to almost twice tile size, with opacity falling from 120 to 6.
It follows the map camera, changes destination with the next tap, and disappears on arrival,
cancellation or a failed route. The map spriteset owns and disposes the marker.
One 32-by-32 bitmap is allocated on the first destination and reused for later
taps; there is no marker allocation before it is needed.

The search expands at most 64 tiles per frame and 1024 per request. A destination
can be walkable but beyond this limit. The character stays still while planning.
Each step rechecks collision; a moving obstacle allows two replans before stopping.
Screen taps prefer the exact tile, then the closest reachable tile within a
two-tile radius if the target is blocked, enclosed or just outside the map.
Equal-distance candidates prefer the shortest walking route. The search remains
bounded; on its work limit it uses the closest candidate reached within that
budget. A tap beyond the map edge tolerance, in the screen's letterbox area, or
with no reachable floor within the radius stops the previous route without
movement. The marker switches to the actual destination when planning finishes.
The direct `Driver.request` API defaults to exact targeting; screen input adds
the tolerance. Obstacle replans keep the original tap and tolerance radius.
Tapping another tile replaces the destination. This does not activate
button-triggered events or items automatically; normal touch events still run.

Any nonzero `Input.dir4` cancels the route immediately, including D-pad and mapped
left-stick directions. The current tile animation finishes normally before the
stock player can take the next manual step. Releasing the direction does not
resume the route. No dedicated cancel button is required. Normal menu/cancel
input, dialogue, map termination, transfers, forced routes, vehicles, position
changes and a clock gap over 250 ms also stop it. Set
`HardRPGTapWalk.enabled = false` to disable it and clear its route.

## Tests

Run `ruby tests/tap-to-walk.rb` for routing and
`ruby tests/tap-to-walk-marker.rb` for marker and resource lifecycle checks.
To exercise actual stock character/player code,
run `ruby tests/tap-to-walk-stock.rb STOCK_DIR`, where `STOCK_DIR` contains your
own extracted VX Ace character base, character and player scripts named
`029.rb`, `030.rb`, and `031.rb`.
For XP/VX, run `ruby tests/tap-to-walk-legacy-stock.rb xp STOCK_DIR` or
`ruby tests/tap-to-walk-legacy-stock.rb vx STOCK_DIR`. XP uses extracted script
files ending in `-Game_Character_1.rb`, `-Game_Character_2.rb`,
`-Game_Character_3.rb` and `-Game_Player.rb`; VX uses `027.rb` and `030.rb`.

Build the original map fixture with
`python3 scripts/build-tap-walk-probe.py OUTPUT --stock-dir STOCK_DIR --engine ace`.
Use `--engine xp` or `--engine vx` for the matching stock scripts.
The resulting archive contains those user-provided scripts. Keep it private;
the repository only contains the original fixture and adapter.

The fixture can accept synthetic input through
`ux0:/data/hardrpg/tap-walk-command.txt` when the test-only marker
`ux0:/data/hardrpg/tap-walk-simulated.txt` exists before launch. Commands are
`ID tap PIXEL_X PIXEL_Y` or `ID direction DIR FRAMES`; IDs must change.
They enter through the RGSS Input interface and never change player coordinates.
The simulated mode uses a logical frame clock to isolate routing from emulator
clock discontinuities; it does not validate touch hardware or suspend behavior.
Without the marker, the fixture delegates entirely to native input and clock.
The fixture reports state at `ux0:/data/hardrpg/tap-walk-state.txt`.

Validate real front-touch coordinates, suspend/resume, visual output, custom-game
movement and frame costs on the Vita before enabling this in a general release.
Engine adapters cover stock movement conventions. Full game scripts, custom
movement/collision systems and gameplay still require their own validation.
