# Validation

HardRPG 0.2.1 Alpha uses the pinned VitaSDK container described in
[BUILDING.md](BUILDING.md). Release checks verify source syntax, static payload
hashes, launcher backend, Title ID, app version, system image formats and the
absence of embedded developer home paths. Source and build information
accompany the release.

## Host checks

Integration checks cover game detection, nested folders, external libraries,
ZIP preparation and cache reuse, save preservation, cancellation, path
validation, display settings and RTP folder/ZIP selection. Native-launcher
checks cover focus, display names, search, engine filters, touch scrolling,
error history and report exports. They also compare game identities, handoffs
and bidirectional cache reuse between the native and RGSS launchers.

Rendering checks cover font coverage/layout, bitmap copies, window texture
allocation and GPU scene ownership. Recovery checks cover Ruby waits,
read-handle reopening, mounted archives, audio-related lifecycle paths and
retained error reports. The desktop preview runs the production menu model,
UI and canvas; it does not emulate Vita game performance.

## Physical Vita checks

Testing through October 6, 2026 confirmed:

- Handled script errors display a backtrace and return to the launcher,
  allowing another game to start.
- Standby and wake retain the running game and audio with USB disconnected.
- Ao Oni retains walls and floors after repeated inventory-menu returns.
- The Vita rename keyboard closes without the earlier flicker and clipping.
- BLACK SOULS II menu decorations and HP/MP bars render correctly; the shared
  fixes also apply to other games.
- BLACK SOULS combat and menu performance improved. BLACK SOULS II and
  Red Hood Woods also showed performance improvements.
- Repeated menu/battle checks retained window backgrounds and decorations.
- The existing game suite was retested on the native launcher without new
  regressions reported.

The suite test preceded the final launcher presentation refinements and ZIP
engine labels. The public package retains those tested runtime fixes and uses
Title ID `HARDRPG01` to update the existing app. The test bubble used a separate
Title ID but shared the same external data layout.

These checks do not establish full-game completion, an exact FPS guarantee,
or every Settings/RTP combination. Long-session stability and game-specific
failures outside the tested routes remain open. See
[game-by-game observations](COMPATIBILITY.md).
