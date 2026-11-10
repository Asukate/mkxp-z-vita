# HardRPG 0.5.2 Alpha

Faster LISA: The Joyful and Ao Oni R, fewer Pocket Mirror crashes, first
playable Pokémon Essentials games, Exit Fate fixes, and Japanese and Chinese in the launcher.

## Games

- **LISA: The Joyful:** the archer cave (map 365) runs at 60 fps, up from
  about 31 ([issue #21](https://github.com/Asukate/mkxp-z-vita/issues/21)).
  Sound effects no longer cut each other off when many play at once.
- **Ao Oni R:** rooms run at 58–60 fps and the Oni's chases no longer slow
  the game down; the Mines still drop to about 55 fps while walking
  ([issue #20](https://github.com/Asukate/mkxp-z-vita/issues/20)).
- **Pocket Mirror:** large pictures need much less memory, and the game no
  longer runs out of memory after playing for a while, including in the
  Rose Labyrinth ([issue #19](https://github.com/Asukate/mkxp-z-vita/issues/19)).
- **N.P.C. Dreams:** faster walking and battles.
- **Exit Fate:** text uses a font with the same letter widths as MS Gothic,
  so the intro and dialogue line up again ([#22](https://github.com/Asukate/mkxp-z-vita/issues/22)); the sky and sea no
  longer show noise ([#23](https://github.com/Asukate/mkxp-z-vita/issues/23)); no stray box at the end of some lines
  ([#24](https://github.com/Asukate/mkxp-z-vita/issues/24)); music that loops at an exact buffer boundary keeps playing
  ([#25](https://github.com/Asukate/mkxp-z-vita/issues/25)).
- **Pokémon Uranium and Pokémon Insurgence** (Pokémon Essentials) are now
  partially supported. Uranium starts, loads saves and plays at 30–50 fps
  depending on the map; Insurgence reaches its title and gameplay. Progression
  has not been tested far.
- Fixed a random crash right after a map change.

## Launcher and controls

- **Japanese and Simplified Chinese** launcher languages. HardRPG picks them
  from the system language, or choose one in **Settings → Language**.
- **In-game keyboard:** tap the keyboard button under the menu button in the
  left border, or let a game that asks for typed text open it by itself. It
  can dock at the bottom or split into two halves, works with touch or the
  D-pad, and has function keys, Ctrl and Alt for games that need them.
- **Your own fonts:** copy a font file you own (for example MS Gothic from
  your PC) into `ux0:/data/hardrpg/fonts`. Games that ask for it by name and
  do not include it use it instead of a stand-in.

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

<!-- 04cb5eceb70899d4680eae55a2bd9dff0ca9e05afdb78521fe50310dc83359a1 -->
