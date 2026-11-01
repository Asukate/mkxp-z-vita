# Faster scene changes in RPG Maker XP games (preload). On the Vita a menu open or close used to
# spend most of its time rebuilding the map behind the menu; two causes are handled here.
#
# 1. Tilesets. The default Spriteset_Map#dispose disposes the tileset and autotile images, which
#    RPG::Cache still lists, so the next Spriteset_Map (menu close, battle end, every menu that
#    draws the map) loads them from disk again (~40 ms on the Vita). HardRPGXPTileKeep holds
#    those images back until the next Spriteset_Map is made: it takes them from the cache, and
#    any it does not use are disposed then. At most one map's tileset is kept extra.
#
# 2. Weather scripts that build their images pixel by pixel and 500 sprites for every
#    Spriteset_Map (~0.2 s each): "Zer0 Advanced Weather" 1.1 and ccoa's weather script (To the
#    Moon). See HardRPGXPWeather below.
#
# Set $hardrpg_xp_tile_keep = false or $hardrpg_xp_weather_cache = false in a per-game shim to
# turn either off.
module HardRPGXPTileKeep
  @held = []
  @deferring = false

  # Extended onto a tileset or autotile image: while a Spriteset_Map is being disposed, its
  # dispose only marks the image as held.
  module Hold
    def dispose
      return super unless HardRPGXPTileKeep.deferring
      HardRPGXPTileKeep.held << self unless HardRPGXPTileKeep.held.any? { |b| b.equal?(self) }
    end
  end

  class << self
    attr_accessor :deferring
    attr_reader :held, :status

    def images(tilemap)
      return [] unless tilemap.is_a?(Tilemap) && !tilemap.disposed?
      [tilemap.tileset, *(0..6).map { |i| tilemap.autotiles[i] }].select do |b|
        b.is_a?(Bitmap) && !b.disposed?
      end
    end

    # Disposes the held images the new spriteset does not use.
    def release(keep)
      held, @held = @held, []
      held.each { |b| b.dispose unless b.disposed? || keep.any? { |k| k.equal?(b) } }
    end

    def install
      return @status = 'off (disabled)' if $hardrpg_xp_tile_keep == false
      return @status = 'off (not XP)' unless defined?(RPG::Cache) && defined?(Spriteset_Map) && defined?(Tilemap)
      # alias_method like the other preloads: mixing prepend with their aliases loops forever.
      Spriteset_Map.class_eval do
        alias_method :hardrpg_tile_keep_initialize, :initialize
        alias_method :hardrpg_tile_keep_dispose, :dispose
        def initialize(*args)
          hardrpg_tile_keep_initialize(*args)
        ensure
          HardRPGXPTileKeep.release(HardRPGXPTileKeep.images(@tilemap))
        end

        def dispose
          images = HardRPGXPTileKeep.images(@tilemap)
          images.each { |b| b.extend(HardRPGXPTileKeep::Hold) unless b.is_a?(HardRPGXPTileKeep::Hold) }
          HardRPGXPTileKeep.deferring = !images.empty?
          hardrpg_tile_keep_dispose
        ensure
          HardRPGXPTileKeep.deferring = false
        end
      end
      @status = 'on'
    end
  end
end

# Zer0 Advanced Weather and ccoa's weather: at the game's first frame, if the game still has an
# unmodified script (checked by bytecode checksum), the images are built once (per colour
# variation for Zer0) and shared by later weather objects, and sprites are made when weather
# needs them (up to the current amount) instead of 500 up front. The picture is the same:
# sprites past the current amount are invisible in the original too, and a new sprite starts in
# the state the original's sprites start in.
module HardRPGXPWeather
  # CRC32 of each original method's bytecode (engine Ruby 3.1), see sprite_fastpath.rb.
  DIGESTS = {
    initialize: 'af91584a', dispose: '4b2707a1', :type= => 'b57c194b', :max= => '18c2855e',
    :ox= => '5a904b85', :oy= => '2c262df5', make_bitmaps: '790f10cc', update_user_defined: 'b19bc27f',
  }
  CCOA_DIGESTS = {
    initialize: '95ccde74', dispose: '867af823', :type= => '3689add2', :max= => '9f72030f',
    :ox= => 'b58d4950', :oy= => '3a500be1', make_bitmaps: '68aa1d06', update_user_defined: '0eef0b0e',
  }
  # Images made by ccoa's make_bitmaps (everything it sets except @sprites and @user_bitmaps).
  CCOA_SHARED = %i[@rain_bitmap @storm_bitmap @snow_bitmap @hail_bitmap @petal_bitmap
                   @blood_rain_bitmap @autumn_leaf_bitmaps @green_leaf_bitmaps @rose_bitmaps
                   @feather_bitmaps @sparkle_bitmaps @meteor_bitmap @impact_bitmap @ash_bitmaps
                   @bubble_bitmaps].freeze
  # Images made by make_bitmaps (everything it sets except @sprites and @user_bitmaps).
  SHARED = %i[@arrow_bitmap @ash_bitmaps @bat_bitmaps @bee_bitmaps @bird_bitmaps @bomb_bitmap
              @bomb_impact_bitmap @bubble2_bitmaps @bubble_bitmaps @butterfly_bitmaps
              @fallingrocks_bitmaps @feather_bitmaps @fish_bitmap @hail_bitmap @impact_bitmap
              @leaf_bitmaps @meteor_bitmap @monostarburst_bitmaps @petal_bitmaps @rain_bitmap
              @rain_splash @sakura_bitmap @snow_bitmap @snow_bitmaps @sparkle_bitmaps
              @starburst_bitmaps @storm_bitmap].freeze
  SPRITES = 500
  CACHE = {}

  class << self
    attr_reader :status

    def digests
      DIGESTS.keys.map { |m| [m, (HardRPGSpriteFastPath.digest(own(m)) rescue nil)] }.to_h
    end

    # RPG::Weather's own method m, past anything prepended in front of it (probes, other patches).
    def own(m)
      meth = RPG::Weather.instance_method(m)
      meth = meth.super_method while meth && meth.owner != RPG::Weather
      meth
    end

    def matches?(digests = DIGESTS)
      return false unless defined?(RPG::Weather) && defined?(HardRPGSpriteFastPath)
      return false if digests.equal?(DIGESTS) && $zer0_adv_weather != 1.1
      digests.all? do |m, want|
        meth = own(m)
        meth && want && HardRPGSpriteFastPath.digest(meth) == want
      end
    rescue NameError
      false
    end

    def install
      if $hardrpg_xp_weather_report
        File.open($hardrpg_xp_weather_report, 'w') { |f| f.puts(digests.inspect) } rescue nil
      end
      return @status = 'off (disabled)' if $hardrpg_xp_weather_cache == false
      return @status = 'off (no weather script)' unless defined?(RPG::Weather)
      if $zer0_adv_weather
        return @status = 'off (changed)' unless matches?
        RPG::Weather.prepend(Lazy)
        return @status = 'on'
      end
      return @status = 'off (unknown weather script)' unless matches?(CCOA_DIGESTS)
      RPG::Weather.prepend(CcoaLazy)
      @status = 'on (ccoa)'
    end

    # ccoa: the shared images, made again if any of them was disposed.
    def ccoa_shared(weather)
      if @ccoa.nil? || @ccoa.any? { |_, v| disposed?(v) }
        weather.make_bitmaps
        @ccoa = CCOA_SHARED.map { |iv| [iv, weather.instance_variable_get(iv)] }.to_h
      else
        @ccoa.each { |iv, v| weather.instance_variable_set(iv, v) }
        weather.instance_variable_set(:@sprites, [])
        weather.instance_variable_set(:@user_bitmaps, [])
        weather.update_user_defined
      end
    end

    # The shared images for a colour variation; made again if any of them was disposed.
    def shared(weather, variation)
      set = CACHE[variation]
      if set.nil? || set.any? { |_, v| disposed?(v) }
        weather.hardrpg_original_make_bitmaps
        CACHE[variation] = SHARED.map { |iv| [iv, weather.instance_variable_get(iv)] }.to_h
      else
        set.each { |iv, v| weather.instance_variable_set(iv, v) }
        weather.instance_variable_set(:@sprites, [])
        weather.instance_variable_set(:@user_bitmaps, [])
        weather.update_user_defined
      end
    end

    def disposed?(v)
      return v.any? { |b| disposed?(b) } if v.is_a?(Array)
      v.respond_to?(:disposed?) && v.disposed?
    end
  end

  module Lazy
    def initialize(viewport = nil)
      $game_screen = Game_Screen.new if $game_screen == nil
      @type = @max = @ox = @oy = @count = 0
      @current_pose, @info, @countarray = [], [], []
      @hardrpg_viewport = viewport
      @hardrpg_bitmap = nil
      HardRPGXPWeather.shared(self, $game_screen.weather_variation)
      SPRITES.times do
        @current_pose.push(0)
        @info.push(rand(50))
        @countarray.push(rand(15))
      end
    end

    def hardrpg_original_make_bitmaps
      make_bitmaps
    end

    # Sprites 0..n exist afterwards (the original's loops use 1..@max; index SPRITES is never made).
    def hardrpg_sprites_upto(n)
      n = [n, SPRITES - 1].min
      while @sprites.size <= n
        i = @sprites.size
        sprite = Sprite.new(@hardrpg_viewport)
        sprite.z = 1000
        sprite.visible = false
        sprite.opacity = 0
        sprite.ox = @ox
        sprite.oy = @oy
        @sprites.push(sprite)
        if @type != 0 && i >= 1
          hardrpg_assign(i, sprite)
          sprite.visible = (i <= @max)
        end
      end
    end

    def dispose
      @sprites.each { |sprite| sprite.dispose }
      @user_bitmaps.each { |image| image.dispose }
      $WEATHER_UPDATE = true
    end

    def type=(type)
      return if @type == type
      @type = type
      bitmap = case @type
               when 1, 5, 9 then @rain_bitmap
               when 2 then @storm_bitmap
               when 3, 16 then @snow_bitmap
               when 4 then @hail_bitmap
               when 6, 7, 8 then @leaf_bitmaps
               when 10 then @sakura_bitmap
               when 11 then @petal_bitmaps[0]
               when 12 then @feather_bitmaps[0]
               when 13 then @butterfly_bitmaps
               when 14 then @sparkle_bitmaps[0]
               when 15 then @user_bitmaps[rand(@user_bitmaps.size)]
               when 17 then @meteor_bitmap
               when 18 then @ash_bitmaps[rand(2)]
               when 19 then @bubble_bitmaps[rand(2)]
               when 20 then @bubble2_bitmaps[rand(4)]
               when 21 then @sparkle_bitmaps[0]
               when 22 then @fallingrocks_bitmaps
               when 23 then @arrow_bitmap
               when 24, 25, 26 then @starburst_bitmaps[0]
               when 27, 28, 29 then @monostarburst_bitmaps[0]
               when 30 then @bomb_bitmap
               when 31 then @bird_bitmaps
               when 32 then @bat_bitmaps
               when 33 then @bee_bitmaps
               when 34 then @fish_bitmap
               when 35 then @raven_bitmaps
               end
      @thunder = true if @type == 5
      @realistic_thunder = true if @type == 9
      @thunder = false if @type != 5
      @realistic_thunder = false if @type != 9
      if $game_switches != nil && $game_variables != nil
        $game_variables[WEATHER_TYPE_VARIABLE] = $game_screen.weather_type
        $game_switches[ADVERSE_WEATHER_SWITCH] = ADVERSE_WEATHER.include?($game_screen.weather_type)
      end
      @hardrpg_bitmap = bitmap
      hardrpg_sprites_upto(@max) if @max > 0
      (1...@sprites.size).each do |i|
        sprite = @sprites[i]
        sprite.visible = (i <= @max)
        hardrpg_assign(i, sprite)
      end
    end

    # What the original type= does for sprite i.
    def hardrpg_assign(i, sprite)
      bitmap = @hardrpg_bitmap
      case @type
      when 3
        r = @info[i] = rand(4)
        sprite.bitmap = @snow_bitmaps[r]
      when 6, 7, 8
        @current_pose[i] = rand(13)
      when 13
        @current_pose[i] = rand(16)
        sprite.bitmap = @butterfly_bitmaps[@current_pose[i]]
      when 19
        sprite.bitmap = @bubble_bitmaps[rand(2)]
      when 20
        sprite.bitmap = @bubble2_bitmaps[rand(4)]
      when 22
        r = @info[i] = rand(3)
        sprite.bitmap = @fallingrocks_bitmaps[r]
      when 31
        @current_pose[i] = rand(4)
        sprite.bitmap = @bird_bitmaps[@current_pose[i]]
      when 32
        @current_pose[i] = rand(16)
        sprite.bitmap = @bat_bitmaps[@current_pose[i]]
      when 33
        @current_pose[i] = rand(16)
        sprite.bitmap = @bee_bitmaps[@current_pose[i]]
      when 34
        @info[i] = rand(200)
        sprite.bitmap = bitmap
      when 35
        @current_pose[i] = rand(16)
        sprite.bitmap = @raven_bitmaps[@current_pose[i]]
      else
        sprite.bitmap = bitmap
      end
    end

    def ox=(ox)
      return if @ox == ox
      @ox = ox
      @sprites.each { |sprite| sprite.ox = @ox }
    end

    def oy=(oy)
      return if @oy == oy
      @oy = oy
      @sprites.each { |sprite| sprite.oy = @oy }
    end

    def max=(max)
      return if @max == max
      @max = [[max, 0].max, SPRITES].min
      hardrpg_sprites_upto(@max) if @max > 0
      (1...@sprites.size).each { |i| @sprites[i].visible = (i <= @max) }
    end
  end

  # ccoa's script. Its own type=, max=, ox=, oy= and update already skip missing sprites, so
  # only the sprite making, the image sharing and dispose change.
  module CcoaLazy
    SPRITES = 500

    def initialize(viewport = nil)
      @type = 0
      @max = 0
      @ox = 0
      @oy = 0
      @count = 0
      @current_pose = []
      @info = []
      @countarray = []
      @hardrpg_viewport = viewport
      HardRPGXPWeather.ccoa_shared(self)
      @hardrpg_last = nil
      SPRITES.times do
        @current_pose.push(0)
        @info.push(rand(50))
        @countarray.push(rand(15))
      end
    end

    # Sprites 0..n exist afterwards (the original's loops use 1..500; index 500 is never made).
    # A new sprite gets what the last type= or update_user_defined gave every sprite.
    def hardrpg_sprites_upto(n)
      n = [n, SPRITES - 1].min
      while @sprites.size <= n
        i = @sprites.size
        sprite = Sprite.new(@hardrpg_viewport)
        sprite.z = 1000
        sprite.visible = false
        sprite.opacity = 0
        sprite.ox = @ox
        sprite.oy = @oy
        @sprites.push(sprite)
        if @hardrpg_last == :user
          sprite.bitmap = @user_bitmaps[rand(@user_bitmaps.size)]
        elsif @hardrpg_last == :type && i >= 1
          sprite.bitmap = @type == 19 ? @bubble_bitmaps[rand(@bubble_bitmaps.size)] : @hardrpg_bitmap
        end
        sprite.visible = (i <= @max) if i >= 1
      end
    end

    def type=(type)
      return if @type == type
      # Sprite 1 shows which image the original chose (some types pick one at random).
      hardrpg_sprites_upto([@max, 1].max)
      super
      @hardrpg_bitmap = @sprites[1].bitmap
      @hardrpg_last = :type
    end

    def max=(max)
      return if @max == max
      wanted = [[max, 0].max, SPRITES].min
      hardrpg_sprites_upto(wanted) if wanted > 0
      super
    end

    def update_user_defined
      super
      @hardrpg_last = :user
    end

    # The shared images stay for the next weather object; the rest goes as in the original.
    def dispose
      @sprites.each { |sprite| sprite.dispose }
      @user_bitmaps.each { |image| image.dispose }
      $WEATHER_UPDATE = true
    end
  end
end

# Installed at the game's first frame, after its scripts have defined the classes.
class << Graphics
  alias_method :hardrpg_xp_switch_update, :update
  def update(*args)
    HardRPGXPTileKeep.install unless HardRPGXPTileKeep.status
    HardRPGXPWeather.install unless HardRPGXPWeather.status
    hardrpg_xp_switch_update(*args)
  end
end
