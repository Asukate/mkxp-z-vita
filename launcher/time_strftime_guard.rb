# Vita Time#strftime guard (preload). Measured failure: the Vita Ruby fork's
# Time value formatting reads garbage native args -- Time#strftime raises
# `TypeError: wrong argument type <garbage> (expected String)` and sometimes
# segfaults outright (4 decoded psp2core dumps, all in Scene_File timestamp
# draw). Time#year/month/day/... and Time#inspect are broken the same way,
# so no pure-Ruby reimplementation is possible: this override never touches
# native formatting and returns an honest placeholder instead.
# Affects display-only timestamps (XP save-file list). Playtime etc. use
# frame counts, not Time, and are unaffected.
class Time
  def strftime(_fmt)
    "----/--/-- --:--"
  end
end
