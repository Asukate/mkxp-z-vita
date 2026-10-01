# Vita compat shim (preloaded before game scripts).
# To the Moon's "Start Game" needs desktop-mkxp's MKXP.data_directory.
# Launcher copy: points at the games/ tree (the census VPK hardcoded the
# census path). Loaded only for to-the-moon via its per-game sidecar.
module MKXP
  def self.data_directory
    "ux0:/data/hardrpg/games/to-the-moon/"
  end
end
