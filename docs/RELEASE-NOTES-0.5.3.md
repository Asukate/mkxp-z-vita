# HardRPG 0.5.3 Alpha

Person of Courage starts, Mysta's Vore RPG fixes, and faster RPG Maker XP
animations and MIDI music for every game.

## Games

- **Person of Courage:** no longer stops at the start with "Error loading
  image 'Graphics/Faces/Actor1': not a PNG"
  ([issue #27](https://github.com/Asukate/mkxp-z-vita/issues/27)). The game
  stores some faces as BMP pictures with a `.png` name; HardRPG now reads a
  picture by its content, as Windows does.
- **Mysta's Vore RPG:** typing a name on the save screen works, text no longer
  runs past the edge of its windows, and maps that slowed to 18–37 fps run at
  the game's own 36–40.
- **RPG Maker XP games:** animations in battles and on maps are kept loaded
  and prepared ahead instead of loading on every attack, and maps with many
  events skip characters that are far off screen.
- **MIDI music:** no more freezes of about 0.7 s on some starts, and stopping
  or changing a song no longer holds the game for up to half a second.
- **Arial, Times New Roman and Courier New:** games that ask for these fonts
  without including them get text sized exactly as on Windows, so long lines
  fit their windows.

## Known issues

- **(Mario) The Music Box:** the picture gallery takes about 28 s to open
  with all 222 pictures unlocked.
- **Hello Charlotte EP1:** some long dialogue lines are cut off by two or
  three letters at the right edge of the message window.
- **Hylics:** holding **L + R + Start** does not open the in-game menu; tap
  the icon in the screen border instead.

## Updating

Install the VPK over **HardRPG**. It uses the same application ID as earlier
releases and keeps existing game folders, saves and settings.

This is still an alpha: compatibility and speed vary by game. See the
[compatibility list](COMPATIBILITY.md).

## Updating

Install the VPK over **HardRPG**. It uses the same application ID as earlier
releases and keeps existing game folders, saves and settings.

This is still an alpha: compatibility and speed vary by game. See the
[compatibility list](COMPATIBILITY.md).

<!-- 174219a5777cea0177b6743ceb989de5b9c9f009b8e2fe5053d03a0379fb0844 -->
