# HardRPG 0.6.1 Alpha

New: achievements for BLACK SOULS and BLACK SOULS II, a smoother
Cat in the Box and fixes for games that stopped at the start.

## New

- **Achievements for BLACK SOULS and BLACK SOULS II (Steam
  editions):** unlocks are recorded on the Vita as on Steam, and a
  popup shows each one as you earn it. The game's details show your
  progress and open a Steam-style list with each achievement's name
  and icon, unlocked ones newest first with the time you got them.
  The names and icons are looked up once, in the background, when
  the Vita is online.
- **Launcher:** a new look for the selected row (a faint ruby tint),
  a refreshed grid view, L and R to page through a game's
  screenshots, wider menus, sharper PlayStation button icons, and a
  Dante 98 choice in the filter panel.

## Fixes

- **Games that stopped with "incomplete format specifier"** (#32): a
  lone % at the end of a text, which older RPG Makers printed as is,
  no longer stops the game. The CG Gallery of Get Violated By EVERY
  Girl You Encounter RPG ~MAX Volume!~ opens again.
- **Country Girl Keiko and other games with a menu of several games
  inside** (#33): picking one of the inner games no longer stops
  with "super: no superclass method 'method_added'", and each inner
  game reads its own files.
- **Cat in the Box** (#35): runs at about 51-60 fps where it dropped
  to 22-32. Its lights, pathfinding and pictures run faster.
- **N.P.C. Dreams and games with layered maps, lights, auras or
  damage popups:** these scripts now run natively, so their maps
  are faster.
- **Out of memory with 16-bit pictures:** games with large animation
  sheets or full-screen pictures no longer run out of memory when a
  picture is copied or drawn.
- **Exit Fate:** no more short pauses every few seconds on busy
  maps. Games now keep more room for new objects between memory
  clean-ups, so the clean-ups come less often.

## Good to know

- **The first start of each VX Ace game after the update** loads its
  save one to three seconds slower than usual, once: HardRPG checks
  the game's scripts again for the new speedups. Later starts are as
  fast as before.

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
- **GitHub:** download HardRPG-0.6.1.vpk from the release page and
  install it over HardRPG.

Either way, it keeps the same application ID and your game folders,
saves and settings stay.

This is still an alpha: compatibility and speed vary by game. See
the [compatibility list](COMPATIBILITY.md).

## Enjoying HardRPG?

- **Star it on GitHub:** a star at github.com/Asukate/mkxp-z-vita
  helps other PS Vita players find HardRPG.

<!-- e9929e12d31cf5968f1f2ee9e15ae0872c398abe7d15b2db554bfcddb6515167 -->
