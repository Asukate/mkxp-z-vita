# Game compatibility

HardRPG runs RPG Maker XP, VX and VX Ace games. Hardware retesting through
October 6, 2026 reported no new regressions and improved performance in BLACK
SOULS, BLACK SOULS II and Red Hood Woods. Earlier game-specific failures are
retained below unless their failing route was explicitly retested.
Compatibility varies by game and version; reaching a title or testing a save
is not a full-game completion claim.

| Game | Observed behavior |
| --- | --- |
| Black Souls 1 | Runs; combat and menu performance substantially improved in 0.2.1. |
| Black Souls 2 | Runs; menu decorations and HP/MP bars corrected, with improved performance. Earlier crashes outside the retested routes remain unverified. |
| FNAFBS Final Mix | Stops after night selection. |
| Hylics | Runs; performance and font issues. |
| The Witch's House | Runs; visual corruption in some areas. |
| To the Moon | Fails when Begin is pressed. |
| Pokemon Infinite Fusion | Fails before the main menu. |
| Red Hood's Woods | Runs; performance improved in 0.2.1. |
| Ao Oni | Runs; repeated inventory returns retain walls and floors with the shared renderer fix. |
| LISA The Painful (original VX Ace edition) | Reaches the main menu, but crashes when starting a new game. The Unity Definitive Edition is not supported. |
| Mogeko Castle (English VX Ace edition) | Runs. |

The README gallery includes captures from earlier hardware testing. See
[validation](VALIDATION.md) for the tested scope. Exact FPS and full-game
completion are not established for this suite.

Hold L + R + Select for one second to return to the launcher. Handled script
errors retain a backtrace that can be viewed and exported from error history.
Native crashes may not produce a report.

The launcher scans `ux0:/data/hardrpg/games/` by default. Its browser can
reference existing games on other folders and storage devices. Settings
can add persistent game search folders and select existing RTP folders or
ZIPs. Game data, RTP and saves are not included in the VPK.

ZIP games are extracted into `ux0:/data/hardrpg/cache/` and reused on later
launches. That working copy may contain saves and is not automatically
deleted. RTP ZIPs are read directly without creating an extracted copy.
Per-game configuration lives under `ux0:/data/hardrpg/config/` and is merged
after the packaged defaults. See [launcher details](../launcher/README.md).

For further tests, record the launcher version/VPK hash, game version and
engine, the route tested, audio, save/load, and any reproducible failure.
The Steam compatibility fallback is not a complete Steamworks implementation.
