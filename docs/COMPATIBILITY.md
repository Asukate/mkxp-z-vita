# Game compatibility

Results from testing on a real PS Vita. Versions are noted for relevant fixes.
Games change between versions and editions, so your copy may behave differently.
"Runs" means the game has been played on the Vita.

| Status | Meaning |
| --- | --- |
| ✅ Runs | Plays well in testing. |
| 🟡 Partial | Plays, with known problems or parts not yet tested. |
| ⛔ Not supported | Does not start, or stops early. |

## RPG Maker XP, VX and VX Ace

Of these, only Ao Oni has been played from start to finish on the Vita. The
others have been played for a while but not end to end, so problems later in
those games are still possible.

| Game | Engine | Status | Notes |
| --- | --- | --- | --- |
| BLACK SOULS | VX Ace | ✅ Runs | |
| BLACK SOULS II | VX Ace | ✅ Runs | Long sessions no longer run out of memory (0.4.5). Large weapon upgrade lists render correctly and no longer fail at the texture-size limit (0.4.7). |
| Shin Megami Tensei VX: The Genesis | VX | ✅ Runs | Loading games, 3D dungeons and crash rescue fixed in 0.4.6. |
| Pocket Mirror (original 2016 VX Ace edition) | VX Ace | 🟡 Partial | Startup crash fixed in 0.4.8. Opening room, movement, item pickup and inventory checked; later progression untested. |
| Hello Charlotte EP1 (English v2.0) | VX Ace | 🟡 Partial | Item popup TypeError fixed in 0.4.8. The popup crash point was checked on a real PS Vita; later progression untested. |
| Ao Oni | XP | ✅ Runs | Played from start to finish. |
| The Witch's House | VX | ✅ Runs | |
| To the Moon | XP | ✅ Runs | Slowdowns in some areas. |
| LISA: The Painful | VX Ace | ✅ Runs | Map W/A/S/D for combo attacks in Controls. |
| FNaF b Final Mix | VX Ace | ✅ Runs | |
| Red Hood's Woods | VX Ace | ✅ Runs | |
| Hylics | VX Ace | ✅ Runs | Faster since 0.4.5, still slow in some areas. Crystal Beach takes about 13 seconds to load. |
| Mogeko Castle (English VX Ace edition) | VX Ace | ✅ Runs | |
| The Endless Empty | VX Ace | ✅ Runs | |
| VorlorN | XP | ✅ Runs | |

## RPG Maker 2000 and 2003 (EasyRPG)

These run through the bundled EasyRPG Player.

| Game | Engine | Status | Notes |
| --- | --- | --- | --- |
| OFF | 2000/2003 | ✅ Runs | |
| Ib | 2000/2003 | ✅ Runs | |
| Yume Nikki | 2000/2003 | ✅ Runs | |
| Flow | 2000/2003 | ✅ Runs | |

## Not supported yet

> [!WARNING]
> **Pokémon Essentials games are a work in progress** and are not playable in
> 0.4.6. Development builds already open some of them; they still need
> performance work and game-specific fixes.

| Game | Engine | Status | Notes |
| --- | --- | --- | --- |
| Pokémon Uranium | XP (Essentials) | ⛔ Not supported | Development builds open it: language menu, data load and intro. |
| Pokémon Insurgence | XP (Essentials) | ⛔ Not supported | Development builds open it: splash screens, title and main menu. |
| Pokémon Infinite Fusion | XP (Essentials) | ⛔ Not supported | Its data is larger than the memory the Vita gives the game; needs memory work. |

## Tested a game?

Tell us about any game you play, **even if it runs fine with no problems**.
There are far more RPG Maker games than can be tested by hand, and every
report helps this list grow.

Open an [issue](https://github.com/Asukate/mkxp-z-vita/issues) with:

- the game's name, and where you got it (Steam, itch.io, a free download)
- the HardRPG version
- how it went: runs well, or what did not work

If something went wrong, an exported error report and a save file from just
before the problem make it much easier to fix; see
[Something went wrong?](../README.md#something-went-wrong).
