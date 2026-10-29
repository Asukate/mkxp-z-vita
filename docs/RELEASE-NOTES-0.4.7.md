# HardRPG 0.4.7 Alpha

## Large window contents

- **BLACK SOULS II:** opening a weapon-upgrade menu no longer crashes when
  its list grows beyond the Vita's texture-size limit
  ([issue #6](https://github.com/Asukate/mkxp-z-vita/issues/6)).
- Large editable window bitmaps keep their complete contents in memory.
  Only the visible region is uploaded for drawing, so scrolling preserves
  the list, icons, text and selection.
- The same window rendering applies to RPG Maker XP, VX and VX Ace.
- Bitmap coordinates beyond 32,767 pixels retain their full range. Memory
  recovery preserves oversized contents while reclaiming disposable copies.

Logical bitmaps remain bounded by available memory. Graphics filters and
other operations requiring a full GPU texture retain the hardware size limit.

## Updating

Install the VPK over **HardRPG**. It uses the same application ID as earlier
releases and keeps existing game folders, saves and settings.

This is still an alpha: compatibility and speed vary by game. See the
[compatibility list](COMPATIBILITY.md).
