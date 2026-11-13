# Game compatibility

Results from testing on a real PS Vita. Versions are noted for relevant fixes.
Games change between versions and editions, so your copy may behave differently.
"Runs" means the game has been played on the Vita.

| Status | Meaning |
| --- | --- |
| ✅ Runs | Plays well in testing. |
| 🟡 Partial | Plays, with known problems or parts not yet tested. |
| ⛔ Not supported | Does not start, or stops early. |

**Frame rates:** each RPG Maker version has its own speed limit. XP games run
at up to **40 fps**, so 40 fps is full speed for an XP game; a few set their
own limit (Pokémon Uranium runs at up to 50). VX and VX Ace games run at up to
**60 fps**. RPG Maker 2000 and 2003 games run at 60 fps.

## RPG Maker XP, VX and VX Ace

Of these, Ao Oni and Hello Charlotte EP1, EP2 and EP3 have been played from start
to finish on the Vita. The others have been played for a while but not end to
end, so problems later in those games are still possible.

| Game | Engine | Status | Notes |
| --- | --- | --- | --- |
| BLACK SOULS | VX Ace | ✅ Runs | |
| BLACK SOULS II | VX Ace | ✅ Runs | Long sessions no longer run out of memory (0.4.5). Large weapon upgrade lists render correctly and no longer fail at the texture-size limit (0.4.7). Reaches its title about 5 seconds sooner, and an interrupted save keeps the old one (0.4.9). Multi-phase boss fights no longer crash (0.5.1, [#18](https://github.com/Asukate/mkxp-z-vita/issues/18)). |
| Shin Megami Tensei VX: The Genesis | VX | ✅ Runs | Loading games, 3D dungeons and crash rescue fixed in 0.4.6. |
| Pocket Mirror (original 2016 VX Ace edition) | VX Ace | 🟡 Partial | Startup crash fixed in 0.4.8. Opening room, movement, item pickup and inventory checked; later progression untested. Menus close in under half a second since 0.5.0 (up to 5 s before). Large pictures need much less memory, and the out-of-graphics-memory crash after some play, including in the Rose Labyrinth, is fixed (0.5.2, [#19](https://github.com/Asukate/mkxp-z-vita/issues/19)). |
| Hello Charlotte EP1 (English v2.0) | VX Ace | ✅ Runs | Item popup TypeError fixed in 0.4.8. Played to the true ending on 0.4.8 (itch.io version, community report), 50–60 fps; bad ending untested. Some long dialogue lines are cut off by two or three letters at the right edge (known issue). |
| Hello Charlotte EP2: Requiem Aeternam Deo (English, itch.io) | VX Ace | ✅ Runs | Played from start to finish on 0.4.8 (community report): about three hours with no failures or frame drops. |
| Hello Charlotte EP3: Childhood's End (English, version unknown) | VX Ace | ✅ Runs | Played from start to finish on 0.4.8 Alpha ([community report #16](https://github.com/Asukate/mkxp-z-vita/issues/16)): no problems or noticeable frame drops reported. |
| Ao Oni | XP | ✅ Runs | Played from start to finish. |
| Ao Oni R | VX Ace | ✅ Runs | Rooms 58–60 fps since 0.5.2 ([#20](https://github.com/Asukate/mkxp-z-vita/issues/20)); the Mines drop to about 55 fps while walking. |
| The Witch's House | VX | ✅ Runs | |
| To the Moon | XP | ✅ Runs | Slowdowns in some areas, shorter since 0.4.9. |
| LISA: The Painful | VX Ace | ✅ Runs | Map W/A/S/D for combo attacks in Controls. |
| LISA: The Joyful | VX Ace | ✅ Runs | The archer cave runs at 60 fps since 0.5.2 (about 31 before, [#21](https://github.com/Asukate/mkxp-z-vita/issues/21)). |
| FNaF b Final Mix | VX Ace | ✅ Runs | |
| Red Hood's Woods | VX Ace | ✅ Runs | |
| Hylics | VX Ace | ✅ Runs | Faster since 0.4.5, still slow in some areas. Crystal Beach takes about 13 seconds to load. |
| Mogeko Castle (English VX Ace edition) | VX Ace | ✅ Runs | |
| The Endless Empty | VX Ace | ✅ Runs | |
| VorlorN | XP | ✅ Runs | |
| (Mario) The Music Box 1.3.1 (itch.io) | XP | 🟡 Partial | Starts since 0.4.9 (it stopped with "Unable to load scripts" before). Title, intro and the mansion checked, including a quick save and resume; later progression untested. Picture colours fixed in 0.5.0; the picture gallery takes about 28 s to open with all 222 pictures unlocked. |
| N.P.C. Dreams | VX Ace | ✅ Runs | Faster walking and battles since 0.5.2; some frame drops remain. |
| Exit Fate | XP | 🟡 Partial | Change Party screen crash fixed in 0.5.0 ([#15](https://github.com/Asukate/mkxp-z-vita/issues/15)), checked with a community save. Fonts, sky and sea noise, a stray box after some lines and a music stall fixed in 0.5.2 ([#22](https://github.com/Asukate/mkxp-z-vita/issues/22)–[#25](https://github.com/Asukate/mkxp-z-vita/issues/25)). |
| Mysta's Vore RPG | XP | 🟡 Partial | Name entry, text cut off at window edges and slow maps (18–37 fps, now the game's own 36–40) fixed in 0.5.3. Later progression untested. |
| Person of Courage | VX | 🟡 Partial | Starts since 0.5.3 ([#27](https://github.com/Asukate/mkxp-z-vita/issues/27)); it stopped with "not a PNG" before. Opening, a busy map and a battle checked; battles pause for about 2 s at the start and at the victory music. Later progression untested. |
| Finding Paradise (Steam) | XP | ✅ Runs | Starts since 0.5.0 ([#17](https://github.com/Asukate/mkxp-z-vita/issues/17)). Intro 39–40 fps, first town map 35–37 fps (XP games run at up to 40); later progression untested. |
| A New Game (RJ273807, Chinese translation) | VX Ace | 🟡 Partial | Starts since 0.4.9 (it stopped with "class variable access from toplevel" before). Later progression untested. |

## RPG Maker 2000 and 2003 (EasyRPG)

These run through the bundled EasyRPG Player.

| Game | Engine | Status | Notes |
| --- | --- | --- | --- |
| OFF | 2000/2003 | ✅ Runs | |
| Ib | 2000/2003 | ✅ Runs | |
| Yume Nikki | 2000/2003 | ✅ Runs | |
| Flow | 2000/2003 | ✅ Runs | |

## Pokémon Essentials

> [!NOTE]
> **Pokémon Essentials support is new in 0.5.2.** These games start and play,
> but some maps and battles run below the game's own speed limit, and
> progression has not been tested far.

| Game | Engine | Status | Notes |
| --- | --- | --- | --- |
| Pokémon Uranium | XP (Essentials) | 🟡 Partial | Starts, loads saves and plays at 30–50 fps depending on the map. Progression untested. |
| Pokémon Insurgence | XP (Essentials) | 🟡 Partial | Reaches its title and gameplay. Progression untested. |
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
