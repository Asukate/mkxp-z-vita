# mkxp-z-vita · HardRPG

<p align="center">
  <img src="docs/images/readme-header.gif" width="900" alt="HardRPG on a PS Vita: the launcher, then To the Moon, LISA, Hylics and other games">
</p>

<p align="center"><b>Play your RPG Maker games on PlayStation Vita</b><br>XP · VX · VX Ace · 2000 · 2003</p>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-PS%20Vita-0070d1?style=for-the-badge" alt="Platform: PS Vita">
  <a href="#status"><img src="https://img.shields.io/badge/Status-Alpha-orange?style=for-the-badge" alt="Status: Alpha"></a>
  <a href="docs/COMPATIBILITY.md"><img src="https://img.shields.io/badge/RPG%20Maker-XP%20%2F%20VX%20%2F%20Ace%20%2F%202000%20%2F%202003-5865f2?style=for-the-badge" alt="RPG Maker: XP, VX, VX Ace, 2000, 2003"></a>
</p>

<p align="center"><a href="#install">Install</a> · <a href="#add-your-games">Add games</a> · <a href="#using-hardrpg">Use</a> · <a href="#per-game-settings-and-controls">Controls</a> · <a href="#compatibility">Compatibility</a> · <a href="#something-went-wrong">Help</a> · <a href="#building">Build</a></p>


**HardRPG** is an RPG Maker game launcher for the PS Vita. Copy your games to
the Vita and play them. It runs XP, VX and VX Ace games natively (a Vita port
of [mkxp-z](https://github.com/mkxp-z/mkxp-z)) and RPG Maker 2000/2003 games
through a bundled [EasyRPG Player](https://easyrpg.org/).

## Install

You need a Vita with homebrew enabled and [VitaShell](https://github.com/TheOfficialFloW/VitaShell).

1. Download the newest **HardRPG** `.vpk` from [Releases](https://github.com/Asukate/mkxp-z-vita/releases) (the release at the top).
2. Copy it to your Vita and install it with VitaShell.
3. Open **HardRPG** once. It creates its folders in `ux0:/data/hardrpg/`.

<details>
<summary>HardRPG closes right away or shows a black screen on start</summary>

HardRPG needs Sony's shader compiler, `libshacccg.suprx`, in `ur0:/data/`.
This is a one-time setup for your console. The easiest way is [VitaDB Downloader](https://github.com/Rinnegatamante/VitaDB-Downloader#changelog),
which installs it for you. You can also follow
[this guide](https://github.com/Rinnegatamante/vitaGL#prerequisites).

</details>

## Add your games

> [!IMPORTANT]
> No games are included. Use games you own.

Copy each game's folder (or a ZIP of it) into `ux0:/data/hardrpg/games/`.
Keep the folder exactly as the game ships: the folder with `Game.ini` (or
`RPG_RT.ini` for 2000/2003) inside it.

```text
ux0:/data/hardrpg/
├── games/
│   ├── My Game/          ← a game folder, as it came
│   └── Another Game.zip  ← ZIPs work too
└── rtp/                  ← only if a game asks for its RTP
    ├── Standard/         RPG Maker XP
    ├── RPGVX/            RPG Maker VX
    ├── RPGVXAce/         RPG Maker VX Ace
    ├── 2000/             RPG Maker 2000
    └── 2003/             RPG Maker 2003
```

**Games stored somewhere else?** Press **Start** in the library to open the
folder browser, go to the folder, and press **Square** to add the games in it.
They stay where they are.

**ZIP games** are unpacked into `ux0:/data/hardrpg/cache/` the first time you
play them. After that you can free the ZIP's space with **Triangle → Delete
the ZIP, keep the game**: the game stays in the list and plays from the
unpacked copy, with its saves and settings.

More detail: [library, ZIP games and settings](launcher/README.md).

### RTP (only if a game needs it)

Some games don't include RPG Maker's standard graphics and sounds, and expect
the **RTP** (Run Time Package) for their engine to be installed separately. If
a game says files are missing, get the RTP for that engine from the official
RPG Maker site:

| Engine | Official download | Copy into |
| --- | --- | --- |
| RPG Maker XP | [xp_rtp104e.exe](https://assets.rpgmakerweb.com/xp_rtp104e.exe) | `rtp/Standard/` |
| RPG Maker VX | [vx_rtp102e.zip](https://assets.rpgmakerweb.com/vx_rtp102e.zip) | `rtp/RPGVX/` |
| RPG Maker VX Ace | [RPGVXAce_RTP.zip](https://assets.rpgmakerweb.com/RPGVXAce_RTP.zip) | `rtp/RPGVXAce/` |
| RPG Maker 2000 | [rpg2000_rtp_installer.exe](https://assets.rpgmakerweb.com/rpg2000_rtp_installer.exe) | `rtp/2000/` |
| RPG Maker 2003 | [rpg2003_rtp_installer.zip](https://assets.rpgmakerweb.com/rpg2003_rtp_installer.zip) | `rtp/2003/` |

These are PC installers, and the Vita can't run them. Unpack the RTP on your
PC first, then copy its folders (`Graphics` and `Audio`, or for 2000/2003
`System`, `Music`, `CharSet` and the rest) into the matching `rtp/` folder.

<details>
<summary><b>Windows</b></summary>

Run the installer. For XP, VX and VX Ace the files end up here; copy what's
inside the folder:

- XP: `C:\Program Files (x86)\Common Files\Enterbrain\RGSS\Standard`
- VX: `C:\Program Files (x86)\Common Files\Enterbrain\RGSS2\RPGVX`
- VX Ace: `C:\Program Files (x86)\Common Files\Enterbrain\RGSS3\RPGVXAce`

For 2000 and 2003, skip installing: open the installer `.exe` with
[7-Zip](https://www.7-zip.org/) and extract it. Copy the extracted folders.

</details>

<details>
<summary><b>Linux and macOS</b> (no Windows needed)</summary>

Install `innoextract` and 7-Zip (`sudo apt install innoextract p7zip-full`
on Debian/Ubuntu, `brew install innoextract p7zip` on macOS). Unzip the
download first if it is a `.zip`, then:

| Engine | Command | Copy the contents of |
| --- | --- | --- |
| XP | `innoextract xp_rtp104e.exe` | `app/` → `rtp/Standard/` |
| VX | `innoextract RPGVX_RTP/Setup.exe` | `app/` → `rtp/RPGVX/` |
| VX Ace | `innoextract RTP100/Setup.exe` | `app/` → `rtp/RPGVXAce/` |
| 2000 | `7z x rpg2000_rtp_installer.exe -oRTP2000` | `RTP2000/` → `rtp/2000/` |
| 2003 | `7z x rpg2003_rtp_installer.exe -oRTP2003` | `RTP2003/` → `rtp/2003/` |

</details>

**Settings → RTP locations** shows which RTPs HardRPG found.

## Status

> [!NOTE]
> HardRPG is in alpha. Many games play well, some have problems, and a few do
> not start. Check the [compatibility list](docs/COMPATIBILITY.md) for your game.

**0.3.0 Alpha** is the current release. What's new:

- **Faster games:** smoother maps, quicker menus and battles.
- **More games run:** To the Moon and LISA now work.
- **RPG Maker 2000/2003** games, through EasyRPG.
- **Your controls, your way:** map any Vita button to any keyboard key, turn
  the touch screen or rear pad into buttons, or tap the map to walk there.
- **Per-game settings:** display, audio and controls for each game.

Full list: [release notes](docs/RELEASE-NOTES-0.3.0.md).

## Using HardRPG

| Button | In the library |
| --- | --- |
| Cross | Play a game, or open a folder or ZIP |
| Circle | Go back |
| Triangle | Settings for the highlighted game |
| Select | Search by name |
| L / R | Show only one engine (XP, VX, Ace, 2000/2003) |
| Square | Rename a game in the list (its files are not touched) |
| Start | Open the folder browser |
| Touch | Drag to scroll, tap to select, tap again to play |

**To leave a game and go back to the library, hold L + R + Select.** Unsaved
progress is lost.

## Per-game settings and controls

Highlight a game and press **Triangle**:

- **Controls:** remap the game's controls (below).
- **Display:** aspect ratio, scaling and filtering.
- **Audio / MIDI:** sound, MIDI reverb and chorus.
- **Performance metrics:** an optional FPS overlay.
- **Rename**, **Game information** and **Play**.
- **Remove from library:** takes the game off the list, then asks whether to
  delete its files too (and, for a ZIP game, its unpacked copy). The answer
  is No unless you choose Yes.
- **Delete the ZIP, keep the game:** for a ZIP game you've already played,
  frees the ZIP's space; the game keeps playing from its unpacked copy.

In **Controls**, every Vita button, and parts of the touch screen and rear pad,
can be set to an RPG Maker button or **any keyboard key**. That makes games
playable that were made for a PC keyboard.

- **Buttons:** pick a button, press Cross, and choose from an on-screen keyboard.
- **Touch screen and rear pad:** use the whole surface as one button, split it
  into left and right halves, or into four corners. Nothing is drawn on screen.
- **Tap to walk:** tap a spot on the map and your character walks there.
- **Test inputs** shows what each button and touch does before you play.

*Example: LISA uses W, A, S and D for combo attacks. Set the rear pad to Four
corners and give each corner one of those keys.*

Details: [controls guide](docs/vita-controls.md).

## Compatibility

HardRPG runs RPG Maker XP, VX, VX Ace, 2000 and 2003 games. Some games stop
with a script error, and very demanding games can run slowly.

> [!WARNING]
> **Pokémon Essentials games** (Pokémon Infinite Fusion and other fan games)
> are a work in progress and **not supported yet**.

See the [compatibility list](docs/COMPATIBILITY.md) for game-by-game results.

## Something went wrong?

When a game stops with an error, HardRPG shows the error when you return to the
library. You can find it again in **Settings → Game error history**, and press
**Square** to export it as a file.

To report a problem, [open an issue](https://github.com/Asukate/mkxp-z-vita/issues)
and include:

- the game's name and version (and where you got it, such as Steam),
- what you were doing when it happened,
- the exported error report, if there is one.

**Optional, but very helpful: attach your save file.** Many problems only
happen in one place, after certain story events, or with certain items. A save
from just before the problem lets it be reproduced in minutes, instead of
replaying the game to reach the same spot.

### Where saves are

| Game | Save location |
| --- | --- |
| A game folder in `games/` | Inside the game's own folder, usually `Save01.rxdata`, `Save01.rvdata` or `Save01.rvdata2` |
| A ZIP game | Inside its unpacked copy in `ux0:/data/hardrpg/cache/` |
| An RPG Maker 2000/2003 game | `ux0:/data/hardrpg/saves/easyrpg/` (files named `Save01.lsd` and so on) |

Some games save in a folder of their own; if you can't find it, mention that
in the report.

### Known issues

- Large games can take a while to start the first time.

## Screenshots

Captured on a PS Vita running HardRPG 0.3.0.

<table>
  <tr>
    <td align="center" width="50%">
      <a href="docs/images/screenshots/to-the-moon.png"><img src="docs/images/screenshots/to-the-moon.png" width="420" alt="To the Moon on PS Vita"></a><br>
      <b>To the Moon</b>
    </td>
    <td align="center" width="50%">
      <a href="docs/images/screenshots/lisa.png"><img src="docs/images/screenshots/lisa.png" width="420" alt="LISA: The Painful on PS Vita"></a><br>
      <b>LISA: The Painful</b>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <a href="docs/images/screenshots/hylics.png"><img src="docs/images/screenshots/hylics.png" width="420" alt="Hylics on PS Vita"></a><br>
      <b>Hylics</b>
    </td>
    <td align="center" width="50%">
      <a href="docs/images/screenshots/the-witchs-house.png"><img src="docs/images/screenshots/the-witchs-house.png" width="420" alt="The Witch’s House on PS Vita"></a><br>
      <b>The Witch’s House</b>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <a href="docs/images/screenshots/ao-oni.png"><img src="docs/images/screenshots/ao-oni.png" width="420" alt="Ao Oni on PS Vita"></a><br>
      <b>Ao Oni</b>
    </td>
    <td align="center" width="50%">
      <a href="docs/images/screenshots/black-souls.png"><img src="docs/images/screenshots/black-souls.png" width="420" alt="BLACK SOULS on PS Vita"></a><br>
      <b>BLACK SOULS</b>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <a href="docs/images/screenshots/black-souls-ii.png"><img src="docs/images/screenshots/black-souls-ii.png" width="420" alt="BLACK SOULS II on PS Vita"></a><br>
      <b>BLACK SOULS II</b>
    </td>
    <td align="center" width="50%">
      <a href="docs/images/screenshots/fnafb-final-mix.png"><img src="docs/images/screenshots/fnafb-final-mix.png" width="420" alt="FNaF b Final Mix on PS Vita"></a><br>
      <b>FNaF b Final Mix</b>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <a href="docs/images/screenshots/mogeko-castle.png"><img src="docs/images/screenshots/mogeko-castle.png" width="420" alt="Mogeko Castle on PS Vita"></a><br>
      <b>Mogeko Castle</b>
    </td>
    <td align="center" width="50%">
      <a href="docs/images/screenshots/red-hoods-woods.png"><img src="docs/images/screenshots/red-hoods-woods.png" width="420" alt="Red Hood’s Woods on PS Vita"></a><br>
      <b>Red Hood’s Woods</b>
    </td>
  </tr>
</table>

## Building

For developers: the [build guide](docs/BUILDING.md) covers the VitaSDK
container, dependencies and VPK packaging. The
[desktop launcher preview](docs/LAUNCHER-PREVIEW.md) runs the launcher on a PC
for quick UI work.

| Directory | Purpose |
| --- | --- |
| `src/`, `binding/`, `shader/`, `assets/` | Engine, Ruby bindings, renderer and built-in resources |
| `backends/` | EasyRPG Player integration for RPG Maker 2000/2003 |
| `launcher/` | Package defaults, scripts loaded into games, LiveArea assets |
| `deps/` | Pinned dependencies and their patches |
| `scripts/`, `ci/` | Build, package, verification and CI |
| `docs/` | Guides, compatibility and README images |

## Credits and licenses

HardRPG is developed by [Asukate](https://github.com/Asukate).
Built on [mkxp-z](https://github.com/mkxp-z/mkxp-z) and Ancurio's original mkxp,
with [EasyRPG Player](https://easyrpg.org/), [VitaSDK](https://vitasdk.org/),
Rinnegatamante's [vitaGL](https://github.com/Rinnegatamante/vitaGL) and
[vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK), Ruby, and the other
libraries listed in [third-party notices](THIRD-PARTY-NOTICES.md).

The engine keeps its GPL-2.0-or-later license; components with GPL v3 and
other terms keep their notices and license texts. See [COPYING](COPYING) and
[LICENSES/](LICENSES/). Games belong to their respective owners.

Header illustration: [Tokyoship, Wikimedia Commons](https://commons.wikimedia.org/wiki/File:PlayStation_Vita_illustration.svg),
[CC BY 3.0](https://creativecommons.org/licenses/by/3.0/), modified by adding a
game image to the screen.
