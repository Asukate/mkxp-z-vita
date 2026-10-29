# HardRPG 0.4.8 Alpha

## Font sizing

- **Pocket Mirror (original 2016 VX Ace edition):** fixed the startup crash
  after the team splash
  ([issue #7](https://github.com/Asukate/mkxp-z-vita/issues/7)).
- Some TrueType font sizing tables were read with the wrong byte order on
  Vita. This could turn ordinary text into an enormous allocation and stop
  the game. The engine now reads those tables correctly for every game.

Pocket Mirror's opening dialogue, first-room movement, item pickup, inventory
and return to the map were checked on a real PS Vita. Later progression has
not been tested.

## Updating

Install the VPK over **HardRPG**. It uses the same application ID as earlier
releases and keeps existing game folders, saves and settings.

This is still an alpha: compatibility and speed vary by game. See the
[compatibility list](COMPATIBILITY.md).
