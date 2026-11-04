# HardRPG 0.5.1 Alpha

A bug-fix update for 0.5.0.

## BLACK SOULS II

- Boss fights with several phases, such as the Deep Sea Knight, no longer
  crash when the next phase starts
  ([issue #18](https://github.com/Asukate/mkxp-z-vita/issues/18)). Checked on
  a real PS Vita with the reporter's save.
- The cause was in Ruby's local time handling: the Vita's clock and Ruby's
  time conversion applied the time zone differently, and Ruby stopped with an
  error when the two disagreed. Any game that reads the local time could hit
  it.

## Known issues

- **(Mario) The Music Box:** the picture gallery takes about 28 s to open
  with all 222 pictures unlocked.
- **Hello Charlotte EP1:** some long dialogue lines are cut off by two or
  three letters at the right edge of the message window.
- **Hylics:** holding **L + R + Start** does not open the in-game menu; tap
  the icon in the screen border instead.

## Updating

Install the VPK over **HardRPG**. It uses the same application ID as earlier
releases and keeps existing game folders, saves and settings.

This is still an alpha: compatibility and speed vary by game. See the
[compatibility list](COMPATIBILITY.md).

<!-- 833a0f25b0d4f0ae69d61a8136f85827d4d9281c269d95b4eac069f4c92a750e -->
