# HardRPG 0.6.0 Alpha

New: Dante 98 games, cheats, save backups and a new in-game menu.
Text is now sized exactly as on Windows, and games use less memory.

## New

- **Dante 98 and Dante 98 II games:** the PC-98 RPG Makers now run
  natively, straight from the game's disk image (.hdi, .fdi, .d88
  and similar) or its own files, with the same in-game menu,
  controls, screen filters, fast forward and play time as other
  games. Peret em Heru and Corpse Party (1996) play at full speed,
  with sound and saving. Other PC-98 disks run on the built-in
  NP2kai.
- **Cheats:** a Cheats page in the in-game menu for XP, VX, VX Ace
  and Pokémon Essentials games: gold, items, weapons and armor,
  party levels and stats, god mode and healing, winning or leaving a
  battle, movement speed, teleport with saved spots, switches and
  variables. The basic cheats work in almost every game; a game that
  changes RPG Maker's own code may not support some of the others.
  Nothing changes until you confirm, and saving in the game keeps
  what the cheats gave you.
- **Turbo buttons:** turn on turbo for any button and holding it
  repeats the press, or map a button to Turbo and hold it with the
  one to repeat. The speed is adjustable (Controls).
- **Save backups:** each time an XP, VX, VX Ace, 2000 or 2003 game
  replaces a save, the old one is kept outside the game folder, 5
  versions per save by default (3 to 20). Restore one from the
  game's **Saves** page or **Settings → Save backups**, even for a
  game removed from the library: add the game again and its saves
  come back. On by default; switch it per game in **Performance and
  speed**.
- **New in-game menu:** fills the 16:9 screen with bigger rows over
  the paused game, blurred behind it. The key picker is bigger, the
  Display page explains each choice, and the right stick mirrors the
  left stick or can be mapped like any button, key or mouse pointer.

## Fixes

- **Text sized as on Windows:** letter widths and font sizes now
  follow what Windows draws, measured on a real Windows PC. Hello
  Charlotte EP1's long lines no longer lose letters at the right
  edge, and Japanese and Chinese text in monospaced fonts is as wide
  as on Windows.
- **Games that started with "NoMethodError: undefined method
  'call'"** (#28): games that switch to fullscreen through Windows
  at the start no longer stop there.
- **Orc Strike** (#29): the first battle no longer stops with a
  NoMethodError. XP and VX games that redefine a class with another
  parent now behave as on Windows.
- **Exit Fate world map** (#31): no longer turns invisible or ends
  the game after long sessions. Tall XP tilesets keep only the rows
  a map uses, about 4 MB instead of 20.
- **Mysta's Vore RPG and other keyboard-only games:** the Enter that
  ends typing a name no longer carries into the next screen and
  loads a save.

## Less memory

- **16-bit images:** game pictures take half the video memory, with
  a fine dither that is hard to see on the Vita screen. **Display →
  Image quality** switches a game back to 32-bit if it ever looks
  wrong there.
- **N.P.C. Dreams and games with layered maps:** map layers that use
  the same tilesets share one copy, so the swamp keeps 32 MB more
  video memory free instead of running out.

## Known issues

- **(Mario) The Music Box:** the picture gallery takes about 28 s to
  open with all 222 pictures unlocked.
- **Person of Courage:** battles pause for about 2 seconds when they
  start and when the victory music plays, while the MIDI sound bank
  loads.
- **Hylics:** holding **L + R + Start** does not open the in-game
  menu; tap the icon in the screen border instead.

## Updating

- **VitaDB Downloader, NeoVitaDB Downloader or VitaForge:** search
  for HardRPG on the Vita and install the update from there. The
  stores can take a few hours or more after the GitHub release to
  show the new version.
- **GitHub:** download HardRPG-0.6.0.vpk from the release page and
  install it over HardRPG.

Either way, it keeps the same application ID and your game folders,
saves and settings stay.

This is still an alpha: compatibility and speed vary by game. See
the [compatibility list](COMPATIBILITY.md).

## Enjoying HardRPG?

- **Star it on GitHub:** a star at github.com/Asukate/mkxp-z-vita
  helps other PS Vita players find HardRPG.
