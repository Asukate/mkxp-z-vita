# HardRPG 0.4.9 Alpha

## Mario The Music Box

- **(Mario) The Music Box 1.3.1:** starts and plays
  ([issue #11](https://github.com/Asukate/mkxp-z-vita/issues/11)). Before, it
  stopped at once with "Unable to load scripts from 'Data/Scripts.rxdata'".
- Its custom `.ari` archive is read, and its video player now uses the Vita's
  own video decoder.
- Games that check the keyboard every frame through Win32 calls run faster.
- An XP window with no window skin and partial opacity no longer crashes.

The title and intro were checked on a real PS Vita. Later progression has not
been tested.

## Random start-up crash

- About one launch in eight could crash while Ruby compiled the game's
  scripts. The cause was in Ruby's compiler on the Vita and is fixed for
  every game.

## Faster XP games

- Map changes keep the tileset and autotiles instead of loading them again.
- Popular weather scripts (Zer0 Advanced Weather and ccoa) reuse their images.
- Images that have not changed keep their textures and load again at once.
- Files inside game archives, and background loading, read the memory card
  less often and stay out of the game's way.
- Freeing graphics memory no longer stalls for 3 to 50 ms.

To the Moon's worst hitches at the start went from 1.4 s and 0.6 s to 0.75 s
and 0.33 s.

## Volume boost

- **Game settings → Audio and MIDI → Volume boost** (Off, 125, 150, 200, 300
  or 400%) for games that stay quiet even at full Vita volume, such as Hylics.
  It is also in the in-game menu's new Audio row and applies at once.
- Loud passages are softened near full volume so they do not crackle.

## Leaving with L + R + Select

- With crash rescue on, holding **L + R + Select** now saves the game where it
  is when it is on the map or in its menu. In a battle, at the title or while
  a message waits, the last automatic copy is kept.
- The library offers **Resume where you left** for that game. **Back to
  library** in the in-game menu still leaves without keeping a copy.

## Game picture and performance counter

- Games that set their own screen size now get the right picture shape at
  once, without opening a menu first.
- The performance counter is one narrow column in the screen border. XP games
  show green from 37 fps (they aim for 40), VX and VX Ace from 55. Frame time
  reads "average / slowest ms".

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
