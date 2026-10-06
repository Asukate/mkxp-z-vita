# HardRPG 0.2.1 Alpha

Released October 6, 2026.

- Native launcher with faster navigation and compact settings.
- Game search, engine filters, scrollbar and front-touch scrolling.
- Display-name editing with the Vita keyboard, without renaming game files.
- Full library paths and engine labels for both folders and ZIP games.
- Timestamped game error history, scrollable backtraces and report export.
- Faster RTP inspection and immediate feedback when launching a game.
- Improved combat performance, menu construction and font glyph caching.
- Fixed window decorations, HP/MP bars and disappearing menu/battle windows.
- Improved image allocation recovery and image-loading error reports.
- Standby recovery for Ruby waits, file/archive handles and audio.
- Bubble artwork and a splash shown only on a fresh app launch.

## Updating from 0.1 Alpha

Install **HardRPG.vpk** with VitaShell over the existing app. Title ID
`HARDRPG01` and data root `ux0:/data/hardrpg/` are unchanged. Existing game
references, RTP locations, display settings, per-game configs, ZIP working
copies and saves remain available; no manual library rescan or migration is
required. Do not uninstall the app or delete its data folder to update.

ZIP working copies may contain saves. They are reused and are not automatically
expired or removed. Games, RTP and saves are not included in the VPK.

This remains an alpha. Compatibility and performance vary by game. Handled
script errors return to the launcher with a report; a native crash may not
produce a backtrace. See [compatibility](COMPATIBILITY.md) and
[validation](VALIDATION.md).
