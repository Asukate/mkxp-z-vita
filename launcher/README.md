# HardRPG launcher

The red Games/Settings/About/Exit menu fills 960×544. The native
**HardRPG 0.2.1 Alpha** uses a C++ launcher; games still run in the
RGSS engine. The original `library.rb`/`picker.rb` launcher remains an
alternative build. Selecting a game restarts the engine into its folder.
About displays the version, author, repository and dated one-line release notes.

## Library

Put user-owned game folders or ZIP files in `ux0:/data/hardrpg/games/`.
Cross opens a folder/ZIP or launches a recognized XP, VX, or VX Ace game.
Circle goes up one level, returning to the left menu at the library root.
Triangle refreshes the current listing. ZIPs may contain a game directly,
a wrapper folder, or multiple game folders. Archives inside archives are
not supported. No games or RTP are included in the package.

Press **Start** to open the folder browser. Cross opens folders, Circle goes
up to the storage picker, and Square scans the current game/collection folder.
The browser offers available `ux0:`, `uma0:`, and `imc0:` storage. Recognized
games appear in the library alongside the default folder's entries. References
persist in `ux0:/data/hardrpg/library-paths.txt`; files and existing saves stay
where they are. Start closes the browser. Scanning is cancellable with Circle,
stops inside recognized game folders, and is bounded to 2048 folders/16 levels.
Choose a closer collection folder if that limit is reached. Missing storage
hides unavailable games without erasing their saved references.

First launch creates `games/`, `rtp/`, `config/`, and `cache/`. Optional RTP
goes under `rtp/Standard`, `rtp/RPGVX`, or `rtp/RPGVXAce`.

In the native launcher, **Select** opens game-name search with the Vita
keyboard. A blank search clears it; Circle cancels without changing it.
**L/R** cycles engine filters, with an explicit active-filter label. Only
the pane holding focus highlights a row. Drag the front touchscreen to scroll
the list; tap to select, then tap again to launch. Full game locations appear
above the list; **L+R** opens all paths if the header cannot fit them.
**Square** changes only a game's display name, preserving its files and saves.

## Settings

**Game display** controls original/fill-screen aspect ratio, fit-screen/whole-pixel
scaling, and nearest/bilinear filtering. Cross or Left/Right changes an option;
Square restores its default. Changes apply on the next game launch and are saved
in `launcher-config.json`, preserving unrelated settings. Per-game configuration
can override display defaults. The launcher keeps its own 960×544 layout.

**RTP locations** shows Detected or Missing for XP, VX and VX Ace. The default
folders above and the legacy `XP`, `VX`, `VXACE` aliases are recognized, including
their `.zip` versions. Cross opens the storage browser; Square selects its current
folder, and Cross selects a ZIP. You can select a pack or a collection containing
the named packs, such as `ux0:/data/mkxp-z-vita/rtp`. References persist in
`rtp-paths.txt`. Square on an RTP setting restores automatic lookup; it never
removes pack files. Triangle refreshes detection.

An RTP ZIP is mounted read-only where it is, without extraction or a cache copy.
It needs the pack's `Graphics` and `Audio` directories, directly or inside a
wrapper directory. A Windows RTP installer executable inside a ZIP is not an
asset pack. Compressed RTP assets are decompressed as the game reads them, which
can affect asset-loading time; actual Vita performance depends on the pack/game.
RTP declared in `Game.ini` is checked before preparing or launching a game.
A missing pack displays a launcher message linking to Settings instead of
restarting into the engine with an invalid RTP mount.

**Game folders** adds extra locations to search at startup and library refresh.
Cross opens the browser and Square selects a folder. The default games folder
always remains available. Extra paths persist in `game-folders.txt`; removing a
reference with Square does not delete games or saves. Start's existing scan/add
browser still adds references to individual games through `library-paths.txt`.

PhysicsFS exposes ZIPs as read-only folders. Selecting a ZIP game prepares
a persistent working copy under `cache/zip-<id>/` so Ruby file access and
ordinary saves work. Preparation shows progress and can be cancelled with
Circle. It needs space for the uncompressed game; an I/O failure removes
the incomplete copy. The original ZIP is untouched. Subsequent launches
reuse the working copy, including saves. A changed ZIP is reported instead
of overwriting that copy; back up its saves before replacing/removing the
cache. Added ZIP games use this same preparation path; adding an ordinary game
folder stores its path and launches it in place, without extraction or copying.
The completed working copy has no expiry or automatic eviction. App updates do
not delete it. Treat it as game/save storage: deleting it can delete that game's
saves. Only incomplete `.partial` preparations are automatically removed.

Per-game settings go in `ux0:/data/hardrpg/config/<id>.json`. The stable ID
is in the launch handoff's `selection.json` and can be computed on the host
with `HardRPG::Node#config_key`. It includes the relative folder and ZIP
member path, so equal basenames in different folders stay separate.
Examples under `config/` and `shims/` are optional source material, not
automatically installed or universal fixes. Rename a copied configuration
example to its game's ID.

**Game error history** in the native launcher retains handled game errors with
the game name and UTC timestamp. Cross opens a trace; Square exports the
selected report to `ux0:/data/hardrpg/reports/error-000001.txt` (numbered
uniquely). Automatic history lives in `errors/` and is not deleted on update
or overwritten by a later error. Older manual exports remain listed. Native
crashes that do not produce a trace cannot be listed.

## Returning and updating

Hold **L + R + Select for a second** while a game is running to restart
into the picker. Unsaved progress is lost. Up/Down move the selection;
Cross selects, and the Exit menu returns to LiveArea.

HardRPG uses Title ID `HARDRPG01` and data root `ux0:/data/hardrpg/`.
Installing an updated VPK replaces the launcher app; games and saves in
the data tree remain external. The native launcher reads the existing library,
RTP paths, display settings and per-game configurations without migration or
manual rescanning. Older launcher IDs and the `mkxp-z-vita`
data root remain separate; this build does not move or remove their data.
Preserve backups of your saves.

See [build instructions](../docs/BUILDING.md),
[desktop preview](../docs/LAUNCHER-PREVIEW.md),
[compatibility](../docs/COMPATIBILITY.md), and [validation](../docs/VALIDATION.md).
