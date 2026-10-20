# Faster character sprites (preload). Every map character has a sprite that the default scripts update
# each frame through several Ruby methods; on the Vita that is the largest part of a busy map's frame
# time. At the game's first frame this file checks, by bytecode checksum, that the game still uses the
# default Sprite_Character code (RPG Maker XP or VX Ace), and only then replaces its update with an
# equivalent shorter one. Any game that changed that code keeps its own update.
#
# What the shorter update skips, with the same picture on screen:
# - VX Ace: a sprite whose character has no graphic and no animation or balloon draws nothing, so its
#   source rectangle, position and blend values are left alone until it gets one. screen_x/screen_y are
#   read once per frame instead of twice.
# - XP: when nothing that feeds a sprite changed since its last update (graphic, position, scroll,
#   pattern, direction, opacity, blend, visibility, jump, priority, animation), the sprite already
#   shows the right thing; only the base sprite update runs.
#
# Set $hardrpg_sprite_fastpath = false in a per-game shim to keep the default update.
module HardRPGSpriteFastPath
  # CRC32 of each default method's bytecode as the engine's Ruby 3.1 compiles it, without line numbers.
  # A different Ruby, or a game that edited the method, gives a different checksum.
  VXA = {
    update: '52fcf90f', update_bitmap: '1047d1a3', graphic_changed?: 'b324582c', update_src_rect: 'cfc2e252',
    update_position: '901fccc9', update_other: '63604991', setup_new_effect: 'ed5b5181', move_animation: '95eeccef',
  }.freeze
  # The original Japanese release has update_position without the animation shift; Enterbrain's
  # VXAce_SP1 patch adds it by wrapping the original. Same behaviour as the English default.
  VXA_JP_POSITION = 'f9220b03'
  VXA_SP1_POSITION = '4c5bfa78'
  # MOG Anti Lag hides off-screen event sprites, then calls the default update kept as mog_anti_lag_update.
  VXA_MOG_UPDATE = 'e8daaae5'
  XP_SPRITE_UPDATE = '7a2df0e4'
  XP_CHARACTER = {
    screen_x: '081b0b95', screen_y: '8c3e76bd', screen_z: 'db1af308', bush_depth: 'b3e51d40', jumping?: '68c834ff',
  }.freeze

  class << self
    attr_reader :status

    def shape(a)
      return a unless a.is_a?(Array)
      if a[0] == 'YARVInstructionSequence/SimpleDataFormat'
        body = a[13].reject { |x| x.is_a?(Integer) || (x.is_a?(Symbol) && x.to_s.start_with?('RUBY_EVENT')) }
        return [a[10], a[11], body.map { |x| shape(x) }]
      end
      # invokesuper records the method name once it has run; compare without it.
      if a[0] == :invokesuper && a[1].is_a?(Hash)
        return [:invokesuper, a[1].reject { |k, _| k == :mid }, *a[2..].map { |x| shape(x) }]
      end
      a.map { |x| shape(x) }
    end

    def digest(meth)
      Zlib.crc32(Marshal.dump(shape(RubyVM::InstructionSequence.of(meth).to_a))).to_s(16).rjust(8, '0')
    end

    # True when klass defines name itself and its bytecode is the expected default.
    def default?(klass, name, want)
      meth = klass.instance_method(name)
      meth.owner == klass && digest(meth) == want
    rescue NameError
      false
    end

    def install
      return @status if @status
      return @status = 'off (disabled)' if $hardrpg_sprite_fastpath == false
      return @status = 'off (no bytecode access)' unless defined?(RubyVM::InstructionSequence) && defined?(Zlib)
      return @status = 'off (no Sprite_Character)' unless defined?(Sprite_Character)
      if defined?(RPG::Sprite) && Sprite_Character.superclass == RPG::Sprite
        install_xp
      elsif defined?(Sprite_Base) && Sprite_Character.superclass == Sprite_Base
        install_vxa
      else
        @status = 'off (unknown Sprite_Character)'
      end
    rescue StandardError => e
      @status = "off (#{e.class})"
    end

    def install_vxa
      target = :update
      unless default?(Sprite_Character, :update, VXA[:update])
        return @status = 'off (custom update)' unless default?(Sprite_Character, :update, VXA_MOG_UPDATE) &&
                                                        default?(Sprite_Character, :mog_anti_lag_update, VXA[:update])
        target = :mog_anti_lag_update
      end
      custom = VXA.keys.reject do |m|
        m == :update || default?(Sprite_Character, m, VXA[m]) ||
          (m == :update_position && default?(Sprite_Character, :update_position, VXA_SP1_POSITION) &&
           default?(Sprite_Character, :vxace_sp1_update_position, VXA_JP_POSITION))
      end
      return @status = "off (custom #{custom.join(', ')})" unless custom.empty?
      base = Sprite_Base.instance_method(:update)
      Sprite_Character.send(:alias_method, :hardrpg_default_update, target)
      Sprite_Character.send(:define_method, target) do
        c = @character
        return hardrpg_default_update unless c
        base.bind_call(self)
        return if @tile_id == 0 && @character_name && @character_name.empty? && @balloon_duration == 0 && !@animation &&
                  c.tile_id == 0 && c.character_name.empty? && c.character_index == @character_index &&
                  c.animation_id <= 0 && c.balloon_id <= 0
        update_bitmap
        update_src_rect
        sx = c.screen_x
        sy = c.screen_y
        move_animation(sx - x, sy - y) if @animation
        self.x = sx
        self.y = sy
        self.z = c.screen_z
        update_other
        update_balloon
        setup_new_effect
      end
      @status = target == :update ? 'on (VX Ace)' : 'on (VX Ace, MOG Anti Lag)'
    end

    def install_xp
      return @status = 'off (custom update)' unless default?(Sprite_Character, :update, XP_SPRITE_UPDATE)
      custom = XP_CHARACTER.reject { |m, want| default?(Game_Character, m, want) }.keys
      return @status = "off (custom Game_Character #{custom.join(', ')})" unless custom.empty?
      Sprite_Character.class_eval do
        alias_method :hardrpg_default_update, :update
        def update
          c = @character
          m = $game_map
          if @hardrpg_seen && @tile_id == c.tile_id && @character_name == c.character_name &&
             @character_hue == c.character_hue && c.real_x == @hardrpg_rx && c.real_y == @hardrpg_ry &&
             m.display_x == @hardrpg_dx && m.display_y == @hardrpg_dy && c.pattern == @hardrpg_pattern &&
             c.direction == @hardrpg_direction && c.opacity == @hardrpg_opacity && c.blend_type == @hardrpg_blend &&
             c.transparent == @hardrpg_transparent && c.animation_id == 0 && !c.jumping? && !@hardrpg_jumping &&
             c.instance_variable_get(:@always_on_top) == @hardrpg_on_top
            super
            return
          end
          hardrpg_default_update
          @hardrpg_seen = true
          @hardrpg_rx = c.real_x
          @hardrpg_ry = c.real_y
          @hardrpg_dx = m.display_x
          @hardrpg_dy = m.display_y
          @hardrpg_pattern = c.pattern
          @hardrpg_direction = c.direction
          @hardrpg_opacity = c.opacity
          @hardrpg_blend = c.blend_type
          @hardrpg_transparent = c.transparent
          @hardrpg_jumping = c.jumping?
          @hardrpg_on_top = c.instance_variable_get(:@always_on_top)
        end
      end
      @status = 'on (XP)'
    end
  end
end

# Installed at the first frame, once every script section has defined its classes.
class << Graphics
  alias_method :hardrpg_sprite_fastpath_update, :update
  def update(*args)
    HardRPGSpriteFastPath.install unless HardRPGSpriteFastPath.status
    hardrpg_sprite_fastpath_update(*args)
  end
end
