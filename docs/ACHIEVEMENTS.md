# HardRPG achievements

Open a game’s details in the carousel to see **Achievements** alongside
play time and screenshots. Tap the progress row, or press Down then Cross,
to open the full list. Game settings also links to the list.

The list looks like Steam's: the cover, the counts and play time over a
progress bar, then the unlocked achievements (newest first, with the date and
time each was earned) and the locked ones. Hidden achievements stay hidden
until earned. Up and Down move, L and R move a page, and touch scrolls.

When a catalog is available, the row shows the earned count, catalog total
and percentage. Hidden achievements count toward the total. With local
unlocks alone, the row shows the earned count. Unlocks are shared across that game's save slots and are stored
on the Vita under `ux0:/data/hardrpg/achievements/`. HardRPG uses the same
library identity for an installed ZIP and its unpacked copy.

The Steam editions of BLACK SOULS and BLACK SOULS II award achievements through
`SteamUserStatsLite`. HardRPG routes the games' calls to a local store. A
gameplay event must call the bridge to award an achievement. Existing save
progress is kept, and loading it will earn a trophy if the game fires the
corresponding event again. Earned IDs are visible even before a catalog is
installed. The achievements stay in HardRPG and can be earned offline.

## Names and icons from Steam

The launcher looks these up on its own, in the background, after the artwork
lookup. It does this once for each game that uses achievements, meaning the
game has started with the achievement bridge or has a catalog. It takes the
Steam app ID from the artwork lookup, or searches the Steam store for the
game's exact name. Then it reads the public achievements page on Steam
Community and downloads both icons of every achievement. No key or account is
needed. Everything is kept in
`ux0:/data/hardrpg/achievements/<game-config-key>/`: `steam.json` and
`icons/`. A game Steam has no achievements for is recorded too, so it is not
asked about again. When the network fails nothing is recorded, and the game
is tried again next time. Icons that failed to download are fetched again
later. Press Square on the achievements page to look the game up again, for
example after changing the launcher language.

## Catalogs

Put `hardrpg-achievements.json` alongside the game's `Game.ini`, including
inside a ZIP. To keep the game files untouched, put the catalog at
`ux0:/data/hardrpg/achievements/<game-config-key>/catalog.json`. The last
launched game's key is the `config` field in
`ux0:/data/hardrpg/selection.json`. The per-key catalog takes priority, then
the one beside `Game.ini`, then the one fetched from Steam. Icons always come
from the Steam record.

On a PC, export a catalog from Steam Community using Python 3.

```sh
python3 scripts/import-steam-achievements.py 3755860 /path/to/game/hardrpg-achievements.json
```

Use App ID `3755860` for BLACK SOULS or `3855540` for BLACK SOULS II.
With `--record`, the output is a folder that gets the same `steam.json` and
`icons/` the launcher downloads. Copy it to
`ux0:/data/hardrpg/achievements/<game-config-key>/`.
Use `--language` with a Steam language name for localized catalog text.
The importer also accepts a saved [GetSchemaForGame response][steam-api]
via `--schema`. The importer creates a new file at the output path.

A hand-authored catalog is an array of objects with the exact in-game ID
and optional display fields.

```json
[
  {
    "id": "FINISH_TUTORIAL",
    "name": "First steps",
    "description": "Complete the tutorial.",
    "hidden": false
  }
]
```

An achievement marked hidden appears as **Hidden achievement** until
earned. Set `"hidden": true` on any entry that should conceal spoilers,
including entries whose source catalog marks them public. A catalog can be up to 256 KiB and contain up to 1,024 entries.

## RGSS game scripts

Call `System.unlock_achievement(id)` at the existing award event. It returns
`true` when the achievement is recorded or was already earned. Use
`System.achievement_unlocked?(id)` to read local progress. An ID is a string
of up to 100 bytes without ASCII control characters. When sharing a game with
other RGSS players, check `System.respond_to?(:unlock_achievement)` before
calling the HardRPG extension.

[steam-api]: https://partner.steamgames.com/doc/webapi/ISteamUserStats#GetSchemaForGame
