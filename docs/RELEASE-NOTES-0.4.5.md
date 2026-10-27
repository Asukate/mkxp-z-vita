# HardRPG 0.4.5 Alpha

## Crash rescue

- If a game stops (an error, a closed app or a power loss), starting it again
  offers to **resume where it was**: at the last map change or battle start.
  You can also start normally or discard the copy.
- The copy is kept in `ux0:/data/hardrpg/rescue/`. Your save files are never
  changed, and saving in the game drops the copy.
- On by default for XP, VX and VX Ace games. Turn it off in **Performance and
  speed**: in Settings for every game, in a game's settings, or in the in-game
  menu.

## Fewer crashes

- **BLACK SOULS II** no longer stops with "out of memory" at the start of a
  battle after a long session ([issue #4](https://github.com/Asukate/mkxp-z-vita/issues/4)).
  When graphics memory runs low, HardRPG now frees the game's cached images
  instead of stopping the game.
- **Hylics:** entering Crystal Beach (map 28) a second time no longer runs out
  of memory.
- An out-of-memory error inside the engine now shows an error report instead
  of closing the app without one.
- Fixed a crash when a game using Ruby threads wrote a file.
- Fixed rare launcher crashes while title music, pictures and the library
  loaded at the same time.

## Faster

- **Maps in VX Ace games run faster.** The default map scripts, and popular
  scripts such as Yanfly Ace Core, MOG Anti Lag, Galv's region scripts and the
  Multilayer script, now run inside the engine when the game's copy is the
  known one. Games with changed scripts run them as before. Measured on the Vita:
  - BLACK SOULS II, map 100: 35 → 52 FPS
  - N.P.C. Dreams, Dream World: 6.5 → 12 FPS
  - Hylics, map 30: 45 FPS standing still, 41 walking (under 30 before)
  - LISA: The Painful, map 50: 60 FPS standing still, 51 walking (at most 55 and 44 before)
- Large maps load faster: Hylics map 28 opens in about 13 seconds instead of
  17 to 20.

## Library

- **List view:** game names only, for the fastest start and scrolling.
  Up/Down move a row, Left/Right a page. The toolbar's view button now cycles
  carousel, grid and list.
- **Settings → Library opens in:** last used, carousel, grid or list.
- The library opens at once from the list saved last time, and checks the
  card for new or removed games in the background.
- RPG Maker MV and MZ games are listed as "MV/MZ, coming soon". HardRPG cannot
  run them yet.

## Known issues

- Pokémon Essentials games (Pokémon Uranium, Insurgence, Infinite Fusion)
  are not playable yet.
- Games whose title music is MIDI have no music in the library.
- The first time a new HardRPG version starts a game, it prepares its graphics
  for a few seconds.

This is still an alpha: compatibility and speed vary by game. See the
[compatibility list](COMPATIBILITY.md).

<!-- 35fdb3dbc7cc03054e90d4fcfa1eabc82983ab02d545451b4c90f7bc6e0cbbb7 -->
