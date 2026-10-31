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

## Ruby boolean compatibility

- **Hello Charlotte EP1 (English v2.0):** fixed the item popup crash
  ([issue #9](https://github.com/Asukate/mkxp-z-vita/issues/9)).
- Some scripts use numbers or other Ruby values for boolean options, such as
  a font's shadow. The engine now follows RGSS and Ruby truthiness: only
  `false` and `nil` disable an option. This correction applies across games.

The popup crash point was checked on a real PS Vita. Later progression has
not been tested.

## Clearer error reports

- Error reports open on the exception, message and backtrace, with runtime
  and memory diagnostics below them. The exception type also appears in the
  report heading.
- Saved history and exported reports retain the complete original content.

## Known issues

- **Hello Charlotte EP1:** some long dialogue lines are cut off by two or
  three letters at the right edge of the message window.

## Updating

Install the VPK over **HardRPG**. It uses the same application ID as earlier
releases and keeps existing game folders, saves and settings.

This is still an alpha: compatibility and speed vary by game. See the
[compatibility list](COMPATIBILITY.md).

<!-- 50736711dd5dbe0738830e8abf26fafc06c8c2c10778b65212d096959db6b877 -->
