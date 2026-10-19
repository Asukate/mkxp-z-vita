# HardRPG 0.3.1 Alpha

Released October 19, 2026. This is a pre-release.

## Fixed

Five crashes reported in
[issue #3](https://github.com/Asukate/mkxp-z-vita/issues/3):

- **The Endless Empty** and **VorlorN** stopped with a syntax error as soon as
  they started. Both now reach their title screens. Scripts written for the
  older Ruby that RPG Maker XP, VX and VX Ace shipped with are now loaded.
- **PALETTA** crashed when a character's movement looked for a path.
- **Little Braver** crashed when entering an area with its own background
  sound volume.
- **Flash Cycling** crashed after about 25 minutes of play. Long sessions no
  longer use up the engine's space for running game events.

The Endless Empty and VorlorN were checked on a PS Vita. The fixes for
PALETTA, Little Braver and Flash Cycling were checked on a PS Vita with test
scripts that do what those games did when they crashed; the games themselves
still need to be played to the same points. If you play one of them, please
report how it went in issue #3.

## Known issues

- Pokémon Essentials games (such as Pokémon Infinite Fusion) do not run yet.

This is still an alpha: compatibility and speed vary by game. See the
[compatibility list](COMPATIBILITY.md). For everything new in 0.3.0, see the
[0.3.0 release notes](RELEASE-NOTES-0.3.0.md).
