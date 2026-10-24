# HardRPG 0.4.0 Alpha

## A new library

- Your games as a carousel of covers, with the highlighted game's background
  behind them. Cross shows its details (year, developer, description, play
  time and screenshots), Cross again plays it. A grid view, engine tabs,
  search, sorting and favourites are in the toolbar.
- Covers, backgrounds, logos, screenshots and details are looked up for each
  new game by itself, in the background, from Steam, DLsite, VNDB and itch.io,
  plus SteamGridDB, IGDB and RAWG with a free key. When no source knows a
  game, its own title screen is used.
- Triangle → Change artwork and info looks a game up again and lets you pick
  the cover, background and logo from what each source found.
- Your own pictures and title music: put `cover`, `hero`, `logo`, `shot1`-`4`
  or `theme.ogg` in `ux0:/data/hardrpg/art/<game name>/`.
- Each game's own title music plays while it is highlighted, read from the
  game's files. Menu sounds and music can be set or turned off in Settings →
  Sound.
- Español and Português (Brasil), and a welcome guide on the first start that
  shows where games go.

## In a game

- In-game menu: hold L + R + Start, or tap the icon in the screen's empty
  border. It pauses the game and has Controls, Display, Performance and speed,
  and Back to library; changes apply at once.
- Fast forward: L + R runs the game at up to twice its speed. Any button can
  also be mapped to it.
- Controls are set on a picture of the Vita, front and back, with the touch
  zones drawn where they are. The RPG Maker picker now has the four
  directions.
- New overlay level, Full stats: frame rate, how busy the game is, CPU and GPU
  speeds, memory, video memory and battery.
- RPG Maker 2000/2003 games (EasyRPG): hold L + R + Select to return to the
  library.

## Faster

- HardRPG and its games run at the Vita's full CPU and GPU speed (444 and
  222 MHz) without an overclocking plugin.
- The app starts in about 3 seconds, also the first time with many ZIP games.
- Games start faster: scripts and RGSS archive tables are kept between boots,
  and graphics are prepared once in the launcher after an update.
- Smoother maps: character sprites update faster in games that use the
  default scripts, and pictures and sound effects a game used before are
  prepared on another CPU core.
- A Sharp screen filter is now the default: crisp pixels without uneven
  rows or columns.
- Quick button presses are no longer lost when a frame is slow: double
  taps and combos (LISA's battle combos, for example) land even while the
  game stutters, and a press reaches the game one frame sooner.
- Smoother battles and menus: window backgrounds and gauges, character
  faces, damage numbers and text draw several times faster.

## Fixes

- Games that use Ruby threads no longer crash.
- Quitting an RPG Maker 2000/2003 game returns to the library.
- Opening a ZIP game's options no longer pauses for several seconds.

## Known issues

- Pokémon Essentials games (Pokémon Uranium, Insurgence, Infinite Fusion)
  are not playable yet.
- Games whose title music is MIDI have no music in the library.
- The first time a new HardRPG version starts a game, it prepares its graphics
  for a few seconds.

This is still an alpha: compatibility and speed vary by game. See the
[compatibility list](COMPATIBILITY.md).
