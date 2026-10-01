# Game compatibility

HardRPG 0.1 Alpha runs RPG Maker XP, VX and VX Ace games. Results below are
observations from physical Vita testing on October 1, 2026.
Compatibility varies by game and version; a title screen or screenshot is
not a full-game completion claim.

| Game | Observed behavior |
| --- | --- |
| Black Souls 1 | Runs; performance issues during combat. |
| Black Souls 2 | Runs, but crashes during gameplay. |
| FNAFBS Final Mix | Stops after night selection. |
| Hylics | Runs; performance and font issues. |
| The Witch's House | Runs; visual corruption in some areas. |
| To the Moon | Fails when Begin is pressed. |
| Pokemon Infinite Fusion | Fails before the main menu. |
| Red Hood's Woods | Launch and gameplay tested. |
| Ao Oni | Runs normally. |
| LISA The Painful (original VX Ace edition) | Reaches the main menu, but crashes when starting a new game. The Unity Definitive Edition is not supported. |
| Mogeko Castle (English VX Ace edition) | Runs. |

All listed games were retested with the 0.1 Alpha release VPK on October 1.
The README gallery includes captures from earlier testing.

Audio and exact FPS on matched game scenes were not explicitly verified in
this report. Hold L + R + Select for one second to return to the picker;
the shortcut was verified from the Black Souls 2 title.

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
