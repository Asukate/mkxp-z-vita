# HardRPG 0.5.0 Alpha

## Finding Paradise

- **Finding Paradise (Steam):** starts, plays and opens its menu
  ([issue #17](https://github.com/Asukate/mkxp-z-vita/issues/17)). Before, it
  closed before its first screen.
- There is no Steam client on the Vita: the game's Steam calls now report
  success and do nothing. Achievements stay off.
- Games that read the keyboard themselves and never update RPG Maker's input
  now get the Vita's buttons as RPG Maker's default keys.
- More Ruby 1.8 script syntax is accepted as XP and VX games wrote it, such as
  `{'key', 'value'}` hashes and `text_size (x).width`.
- Large saves are written in pieces: Finding Paradise's autosave pauses play
  for about 0.5 s instead of up to 1.8 s.

The intro runs at 39–40 fps (the game aims for 40) and the first town map at
35–37 fps. Later progression has not been tested.

## Exit Fate

- **Exit Fate:** the Change Party screen opens again
  ([issue #15](https://github.com/Asukate/mkxp-z-vita/issues/15)). It quit
  with NoMethodError when the party had fewer than six members. Checked on a
  real PS Vita with a community save.
- XP and VX games get back more Ruby 1.8 methods that newer Ruby removed:
  `Object#id`, `Object#type`, `String#each`, `Array#nitems` and
  `indexes`/`indices`. A game's own versions keep working.

## JPEG colours

- Red and blue were swapped in every JPEG picture, in every game: Mario's cap
  showed blue, and To the Moon's sunsets came out blue and its daytime sky
  orange. JPEG pictures now show their real colours.

## Faster menus in games with large pictures

- Pocket Mirror's map-sized light and shadow pictures were loaded again every
  time a menu closed, taking 1.5 to 5.3 s. Closing a menu now takes under half
  a second. BLACK SOULS and To the Moon are unchanged.

## Compatibility

- **Hello Charlotte EP3: Childhood's End** played from start to finish
  ([community report #16](https://github.com/Asukate/mkxp-z-vita/issues/16)).

## Known issues

- **Hello Charlotte EP1:** some long dialogue lines are cut off by two or
  three letters at the right edge of the message window.
- **Hylics:** holding **L + R + Start** does not open the in-game menu; tap
  the icon in the screen border instead.

## Updating

Install the VPK over **HardRPG**. It uses the same application ID as earlier
releases and keeps existing game folders, saves and settings.

This is still an alpha: compatibility and speed vary by game. See the
[compatibility list](COMPATIBILITY.md).

<!-- 995e48edcd62a656f69f0dd1b8d78a5b1d3a2fd1e71672f4b7d944c27ba69d1e -->
