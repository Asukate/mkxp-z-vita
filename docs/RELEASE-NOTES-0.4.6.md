# HardRPG 0.4.6 Alpha

## Compatibility fixes

- **Shin Megami Tensei VX: The Genesis** starts and plays. Fixed problems
  with its Ruby compatibility checks, loading saved games, 3D dungeons and
  resuming through crash rescue.
- **BLACK SOULS II:** long weapon upgrade lists now show their weapons and
  icons instead of a blank panel. The fix applies to large editable bitmaps
  in other games too ([issue #5](https://github.com/Asukate/mkxp-z-vita/issues/5)).

## Faster drawing and loading

- Faster bitmap copies, transparent wall images and stretched colour tints.
- 3D dungeons avoid drawing the same view again when it has not changed.
- Games that check for many optional images no longer repeatedly scan
  unchanged folders for missing files. Files written by games are still
  detected.
- Faster loading in games using KGC Passive Skills when no passive skill
  needs an actor copy.

## Error reports

- Windows error dialogs used by game scripts now appear in HardRPG's error
  reports instead of closing the game without their message.

## Updating

Install the VPK over **HardRPG**. It uses the same application ID as earlier
releases, and keeps the existing game folders, saves and settings.

This is still an alpha: compatibility and speed vary by game. Pokémon
Essentials games remain unsupported. See the
[compatibility list](COMPATIBILITY.md).
