# Native Pokemon Essentials methods (preload, RPG Maker XP). The engine
# (binding/essentials-natives.cpp) has C versions of methods every Essentials map runs many times a
# frame: Game_Map#terrain_tag / bush? / deepBush? / in_range?, Game_Character#bush_depth,
# Spriteset_Map#in_range?, ReflectedSprite#update and the SpriteResizer Sprite readers and writers.
# After each game script section this file looks at those methods: one whose text (comments and
# spacing ignored) is a known one is swapped for the C version of that text. A game that edited a
# method keeps its own. Uranium's terrain checks also lean on PBTerrain.isBridge?: they are native
# only while that is the default one-liner, and the Ruby methods come back if a later section
# changes it.
#
# It also gives Essentials' LargePlane (fog, panorama and menu backgrounds) the engine's Plane:
# LargePlane cleared and re-tiled a screen-sized bitmap with blt every time ox/oy changed, which
# a scrolling fog does every frame (Pokemon Uranium's Amatree Town: about a sixth of the frame).
# The engine Plane tiles on the GPU. Only the known LargePlane texts are replaced.
#
# Set $hardrpg_essentials_natives = false in a per-game shim to keep every method in Ruby.
module HardRPGEssentialsNatives
  # "Class#method:crc32:bytes" of the normalized text => native variant.
  KNOWN = {
    # Pokemon Uranium 1.3
    'Game_Map#terrain_tag:2222882543:414' => 'uranium',
    'Game_Map#bush?:1299119176:283' => 'uranium',
    'Game_Map#deepBush?:1121049632:335' => 'uranium',
    'Game_Map#in_range?:1358633751:389' => 'uranium',
    'Game_Character#bush_depth:272286521:337' => 'uranium',
    'Spriteset_Map#in_range?:3218610450:409' => 'uranium',
    'ReflectedSprite#update:2982331140:1538' => 'uranium',
    'Sprite#x=:3741133143:297' => 'resizer',
    'Sprite#y=:3026339993:297' => 'resizer',
    'Sprite#zoom_x=:1022030647:298' => 'resizer',
    'Sprite#zoom_y=:4288316903:298' => 'resizer',
    'Sprite#ox=:832596738:52' => 'uranium',
    'Sprite#oy=:1938941871:52' => 'uranium',
    # Pokemon Insurgence 1.2 (Essentials v16 texts)
    'Game_Map#terrain_tag:3758153763:353' => 'classic',
    'Game_Map#bush?:4185346874:282' => 'classic',
    'Game_Map#in_range?:3856815067:452' => 'classic',
    'Game_Character#bush_depth:3417054257:136' => 'classic',
    'Spriteset_Map#in_range?:2400115730:472' => 'classic',
    'ReflectedSprite#update:1047977839:1435' => 'classic',
    'Sprite#x=:1848552780:299' => 'resizer',
    'Sprite#y=:398091883:299' => 'resizer',
    'Sprite#zoom_x=:1568252977:225' => 'resizer',
    'Sprite#zoom_y=:1067479762:225' => 'resizer',
    'Sprite#ox=:3586886761:136' => 'classic',
    'Sprite#oy=:2624039470:136' => 'classic',
    # Both: the SpriteResizer readers
    'Sprite#x:525213658:22' => 'resizer',
    'Sprite#y:2266732973:22' => 'resizer',
    'Sprite#ox:1721453728:24' => 'resizer',
    'Sprite#oy:3330210926:24' => 'resizer',
    'Sprite#zoom_x:195740357:31' => 'resizer',
    'Sprite#zoom_y:1803916947:31' => 'resizer',
    # Both: RPG::Sprite moving its battle animations along
    'RPG::Sprite#x=:4045655542:107' => 'default',
    'RPG::Sprite#y=:1157932205:107' => 'default',
    # One-liners (Uranium, Insurgence)
    'Game_Character#map:3075929596:40' => 'default',
    'Game_Character#map:1534480290:38' => 'default',
    'Game_Map#data:1655852987:25' => 'default',
    # Pokemon Uranium 1.3: the character sprite
    'Sprite_Character#update:2582517344:2132' => 'uranium'
  }.freeze
  # Methods defined on one line, which Norm does not read: "Class#method" => [line, variant].
  LINES = {
    'Game_Map#data' => ['def data; return @map.data; end', 'default'],
    'PokemonSystem#tilemap' => ['def tilemap; return MAPVIEWMODE; end', 'default']
  }.freeze
  TARGETS = (KNOWN.keys.map { |k| k.sub(/:\d+:\d+\z/, '') } + LINES.keys).uniq.freeze
  # Natives that are right only while PBTerrain.isBridge? is the default.
  NEEDS_BRIDGE = { 'uranium' => %w[Game_Map#terrain_tag Game_Map#bush? Game_Map#deepBush?] }.freeze
  IS_BRIDGE = ['def PBTerrain.isBridge?(tag)', 'return tag==PBTerrain::Bridge', 'end'].freeze
  # LargePlane class texts (normalized, "crc32:bytes"): Uranium 1.3, Insurgence 1.2.
  BITMAP_PATHS = {}
  EXISTS_PATHS = {}
  ANIM_FILES = %w[Data/PkmnAnimations.rxdata Data/PkmnAnimations.rvdata].freeze
  ANIM_CACHE = {}
  LARGE_PLANE = %w[1734359036:2994 2416912306:2886].freeze

  @installed = {} # "Class#method" => variant
  @seen = {}      # "Class#method" => source_location already judged

  class << self
    attr_reader :installed

    def status
      return 'off' if $hardrpg_essentials_natives == false
      @installed.empty? ? 'none' : @installed.map { |n, v| "#{n} (#{v})" }.join(', ')
    end

    def section_text(file)
      return nil unless file.to_s =~ /\ASection(\d+)\z/ && $RGSS_SCRIPTS.is_a?(Array)
      entry = $RGSS_SCRIPTS[$1.to_i]
      entry.is_a?(Array) && entry[3].is_a?(String) ? entry[3] : nil
    end

    def norm(line)
      HardRPGNative::Norm.norm_line(line)
    end

    def crc_key(text)
      "#{Zlib.crc32(text)}:#{text.bytesize}"
    end

    # The default PBTerrain.isBridge? (`return tag==PBTerrain::Bridge`), or false.
    def default_is_bridge?
      return false unless defined?(PBTerrain) && PBTerrain.respond_to?(:isBridge?)
      file, line = PBTerrain.method(:isBridge?).source_location
      src = file && section_text(file)
      return false unless src
      lines = src.lines[line - 1, 3] || []
      lines.map { |l| norm(l.chomp) } == IS_BRIDGE
    end

    def klass_of(cname)
      Object.const_defined?(cname) ? Object.const_get(cname) : nil
    rescue NameError
      nil
    end

    def variant_for(klass, key, name)
      um = klass.instance_method(name)
      file, line = um.source_location
      return nil unless file && um.owner.equal?(klass)
      src = section_text(file)
      return nil unless src
      if (one = LINES[key]) && norm(src.lines[line - 1].to_s.chomp) == one[0]
        return one[1]
      end
      # Norm names a method by its innermost class: RPG::Sprite#x= is "Sprite#x=" there.
      text, at = HardRPGNative::Norm.methods(src)[key.sub(/\A[^#]*::/, '')]
      return nil unless text && at == line
      KNOWN["#{key}:#{crc_key(text)}"]
    end

    # Code a native leaves to Ruby, taken from the game's own lines of that method.
    # Uranium's Sprite_Character#update: the branch that loads a new graphic becomes
    # __hardrpg_sc_changed (everything between the 4-line `if @tile_id!=@character.tile_id or ...`
    # condition and its `end`).
    def helpers(klass, key, variant, loc)
      return true unless key == 'Sprite_Character#update' && variant == 'uranium'
      file, line = loc
      lines = section_text(file).lines
      head = (line...lines.size).find { |i| lines[i] =~ /\A(\s*)if @tile_id!=@character\.tile_id or\s*\z/ }
      return false unless head
      ind = lines[head][/\A\s*/]
      cond_end = (head...lines.size).find { |i| lines[i].include?('@oldbushdepth!=@character.bush_depth') }
      stop = cond_end && (cond_end + 1...lines.size).find { |i| lines[i] =~ /\A#{ind}end\b/ }
      return false unless stop && cond_end == head + 3
      body = lines[cond_end + 1...stop].join
      klass.class_eval("def __hardrpg_sc_changed\n#{body}end\n", file, cond_end + 1)
      true
    rescue StandardError, SyntaxError
      false
    end

    def bridge_dependent?(key, variant)
      (NEEDS_BRIDGE[variant] || []).include?(key)
    end

    def check
      return false if $hardrpg_essentials_natives == false
      return false unless defined?(HardRPGEssentialsNative) && defined?(HardRPGNative::Norm)
      changed = false
      bridge_ok = nil
      TARGETS.each do |key|
        cname, name = key.split('#', 2)
        klass = klass_of(cname)
        next unless klass.is_a?(Class) && klass.method_defined?(name)
        loc = klass.instance_method(name).source_location
        if loc.nil?
          # Ours (a C method) or the engine's: drop ours if what it leans on changed.
          variant = @installed[key]
          next unless variant && bridge_dependent?(key, variant)
          bridge_ok = default_is_bridge? if bridge_ok.nil?
          next if bridge_ok
          HardRPGEssentialsNative.restore(klass, key)
          @installed.delete(key)
          @seen.delete(key)
          changed = true
          next
        end
        @installed.delete(key) # a later section redefined it: that Ruby method is in place now
        next if @seen[key] == loc
        variant = variant_for(klass, key, name)
        if variant && bridge_dependent?(key, variant)
          bridge_ok = default_is_bridge? if bridge_ok.nil?
          next unless bridge_ok # PBTerrain not read yet: judge again after the next section
        end
        @seen[key] = loc
        next unless variant && helpers(klass, key, variant, loc)
        next unless HardRPGEssentialsNative.replace(klass, key, variant)
        @installed[key] = variant
        changed = true
      end
      changed = true if large_plane
      changed = true if resolve_cache
      changed = true if by_lines
      changed = true if rtp_exists_cache
      changed = true if battle_sprites
      changed = true if animations_cache
      changed = true if animations_preload
      changed
    end

    # Methods matched by their own lines (def to the `end` at the same indent), for what Norm cannot
    # name: top-level defs, and Uranium's weather section (a one-line def there confuses it).
    # key => [owner, method name, {"crc:bytes" => variant}]
    BY_LINES = {
      'Object#pbLoadMetadata' => [-> { Object }, :pbLoadMetadata, { '1770719137:281' => 'default' }],
      'Object#pbGetMetadata' => [-> { Object }, :pbGetMetadata,
                                 { '3825333602:120' => 'default', '1089455556:135' => 'default' }],
      'PBDayNight.self#getTone' => [-> { defined?(PBDayNight) && PBDayNight.singleton_class }, :getTone,
                                    { '3039246813:289' => 'uranium' }],
      'Object#pbDayNightTint' => [-> { Object }, :pbDayNightTint, { '1490338471:389' => 'uranium' }],
      'RPG::Weather#update' => [-> { defined?(RPG::Weather) && RPG::Weather }, :update,
                                { '1203654943:1403' => 'uranium' }]
    }.freeze

    def def_text(loc)
      lines = section_text(loc[0])&.lines
      return nil unless lines && lines[loc[1] - 1]
      i = loc[1] - 1
      ind = lines[i][/\A\s*/]
      j = (i + 1...lines.size).find { |k| lines[k] =~ /\A#{ind}end\b/ }
      j && lines[i..j].map { |l| norm(l.chomp) }.join("\n")
    end

    def by_lines
      changed = false
      BY_LINES.each do |key, (owner, name, crcs)|
        klass = owner.call
        next unless klass.is_a?(Module) && (klass.method_defined?(name) || klass.private_method_defined?(name))
        um = klass.instance_method(name)
        loc = um.source_location
        if loc.nil? # ours, or the engine's
          next
        end
        @installed.delete(key)
        next if @seen[key] == loc
        @seen[key] = loc
        next unless um.owner.equal?(klass)
        text = def_text(loc)
        variant = text && crcs[crc_key(text)]
        next unless variant && HardRPGEssentialsNative.replace(klass, key, variant)
        @installed[key] = variant
        changed = true
      end
      changed
    rescue StandardError
      changed
    end

    # pbResolveBitmap (Uranium 051, Insurgence 059: the same text) opens up to 9 files to find one
    # graphic, and Game_Player#character_name calls it every frame the player stands still. Graphics
    # do not change while a game runs, so its answer for a "Graphics/" path is kept.
    RESOLVE_BITMAP = '312823582:502'.freeze
    def resolve_cache
      return false unless Object.private_method_defined?(:pbResolveBitmap)
      loc = Object.instance_method(:pbResolveBitmap).source_location
      return false if loc.nil? || @resolve_seen == loc
      @resolve_seen = loc
      lines = section_text(loc[0])&.lines
      return false unless lines
      i = loc[1] - 1
      j = (i...lines.size).find { |k| lines[k] =~ /\Aend\b/ }
      return false unless j && crc_key(lines[i..j].map { |l| norm(l.chomp) }.join("\n")) == RESOLVE_BITMAP
      Object.class_eval do
        alias_method :__hardrpg_pbResolveBitmap, :pbResolveBitmap
        def pbResolveBitmap(x)
          return __hardrpg_pbResolveBitmap(x) unless x.is_a?(String) && x.start_with?('Graphics/', 'Graphics\\')
          c = HardRPGEssentialsNatives::BITMAP_PATHS
          return (r = c[x]) ? r.dup : nil if c.key?(x)
          r = __hardrpg_pbResolveBitmap(x)
          c.clear if c.size >= 4096
          c[x.dup.freeze] = r.is_a?(String) ? r.dup.freeze : r
          r
        end
        private :pbResolveBitmap, :__hardrpg_pbResolveBitmap
      end
      true
    rescue StandardError
      false
    end

    # RTP.exists? (Uranium 051, Insurgence 059) probes up to 7 file names on the card; every sound
    # effect (FileTest.audio_exist? in se_play, e.g. bumping into a wall) goes through it. Kept the
    # same way for "Audio/" and "Graphics/" paths.
    RTP_EXISTS = '521028762:223'.freeze
    def rtp_exists_cache
      return false unless defined?(RTP) && RTP.is_a?(Module) && RTP.respond_to?(:exists?)
      loc = RTP.method(:exists?).source_location
      return false if loc.nil? || @rtp_seen == loc
      @rtp_seen = loc
      src = section_text(loc[0])
      text, at = src && HardRPGNative::Norm.methods(src)['RTP.self#exists?']
      return false unless text && at == loc[1] && crc_key(text) == RTP_EXISTS
      class << RTP
        alias_method :__hardrpg_exists?, :exists?
        def exists?(filename, extensions = [])
          unless filename.is_a?(String) && filename.start_with?('Audio/', 'Graphics/', 'Audio\\', 'Graphics\\') &&
                 extensions.is_a?(Array)
            return __hardrpg_exists?(filename, extensions)
          end
          c = HardRPGEssentialsNatives::EXISTS_PATHS
          key = extensions.empty? ? filename : "#{filename}\0#{extensions.join("\0")}"
          r = c[key]
          return r unless r.nil?
          r = __hardrpg_exists?(filename, extensions)
          c.clear if c.size >= 4096
          c[key.frozen? ? key : key.dup.freeze] = r
          r
        end
      end
      true
    rescue StandardError
      false
    end

    # Stock Essentials battle code (pbCommonAnimation, pbAnimation, pbThrow) reads the whole
    # Data/PkmnAnimations file with load_data for every animation it plays: 2 MB of Marshal data,
    # tens of thousands of Ruby objects each time. On the Vita that churn fragments the heap until
    # Ruby cannot get a page (Uranium: out-of-memory on the first attack). The battle only reads
    # it, so one loaded copy is kept and handed out again while the file is unchanged.
    def animations_cache
      return false if @anim_cache_done
      return false unless Kernel.private_method_defined?(:load_data) && !Kernel.private_method_defined?(:__hardrpg_load_data)
      @anim_cache_done = true
      Kernel.module_eval do
        alias_method :__hardrpg_load_data, :load_data
        def load_data(path, *rest, &block)
          return __hardrpg_load_data(path, *rest, &block) unless rest.empty? && !block &&
                                                              HardRPGEssentialsNatives::ANIM_FILES.include?(path)
          stamp = (File.mtime(path).to_f rescue nil)
          hit = HardRPGEssentialsNatives::ANIM_CACHE[path]
          return hit[1] if hit && hit[0] == stamp
          HardRPGEssentialsNatives::ANIM_CACHE.delete(path)
          # ~50k objects in one go: collecting while they arrive only walks a growing heap
          gc_was_off = GC.disable
          begin
            data = __hardrpg_load_data(path)
          ensure
            GC.enable unless gc_was_off
          end
          HardRPGEssentialsNatives::ANIM_CACHE[path] = [stamp, data]
          data
        end
        private :load_data, :__hardrpg_load_data
      end
      @installed['load_data PkmnAnimations'] = 'cache'
      true
    rescue StandardError
      false
    end

    # Loads the animations once, as soon as their classes exist (script load, heap still in one
    # piece), so the first attack does not need ~50k new objects from a fragmented heap.
    def animations_preload
      return false if @anim_preloaded || !@anim_cache_done
      return false unless defined?(PBAnimations) && defined?(PBAnimation) && defined?(PBAnimTiming)
      @anim_preloaded = true
      # FileTest does not see files inside a game archive: just try each name.
      t = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      path = ANIM_FILES.find do |f|
        load_data(f)
        true
      rescue Errno::ENOENT, IOError, RuntimeError
        false
      end
      unless path
        @installed['PkmnAnimations preload'] = 'no file'
        return false
      end
      @installed['PkmnAnimations preload'] = format('%s %.0f ms', path, (Process.clock_gettime(Process::CLOCK_MONOTONIC) - t) * 1000)
      true
    rescue StandardError, NoMemoryError => e
      ANIM_CACHE.clear
      @installed['PkmnAnimations preload'] = "failed: #{e.class}: #{e.message[0, 120]}"
      false
    end

    # Uranium's battlers (AnimatedBitmapWrapper#update and DynamicPokemonSprite#update) redraw the
    # animation frame, clone it twice and build a new skewed shadow every frame, about 1.4 MB of new
    # images per frame that only the GC frees (10 fps and an out-of-memory in battle). The patched
    # versions do the same work only when a picture changed: the frame redraw is skipped when the
    # frame and both images are unchanged, and the copies are remade only when the source, the angle,
    # the performance option or the images on the sprites changed (Bitmap#__hardrpg_version).
    BATTLE_SPRITES = {
      'AnimatedBitmapWrapper#update' => '2424681686:542',
      'DynamicPokemonSprite#update' => '1679676366:509'
    }.freeze
    SKEW = '1793302624:413'.freeze # Uranium's Sprite#skew
    # The shadow is a plain Sprite and Sprite#skew is still the text matched at install.
    def skew_ok?(sprite)
      @skew_loc && sprite.instance_of?(Sprite) && Sprite.instance_method(:skew).source_location == @skew_loc
    end

    def battle_sprites
      return false unless Bitmap.method_defined?(:__hardrpg_version)
      changed = false
      BATTLE_SPRITES.each do |key, crc|
        cname, name = key.split('#', 2)
        klass = klass_of(cname)
        next unless klass.is_a?(Class) && klass.method_defined?(name)
        loc = klass.instance_method(name).source_location
        next if loc.nil? || @seen[key] == loc
        @seen[key] = loc
        next unless crc_key(def_text(loc).to_s) == crc
        if key.start_with?('Dynamic') && Sprite.method_defined?(:skew) && Bitmap.method_defined?(:__hardrpg_skew_rows)
          sl = Sprite.instance_method(:skew).source_location
          @skew_loc = sl if sl && crc_key(def_text(sl).to_s) == SKEW
        end
        klass.class_eval(key.start_with?('Animated') ? ABW_UPDATE : DPS_UPDATE, 'essentials_natives.rb (battle)', 1)
        @installed[key] = 'uranium'
        changed = true
      end
      changed
    rescue StandardError
      changed
    end

    ABW_UPDATE = <<~'RUBY'
      def update
        return false if @actualBitmap.disposed?
        return false if @speed < 1
        case @speed
        when 1 then @frames=3
        when 2 then @frames=5
        when 3 then @frames=6
        when 4 then @frames=3
        when 5 then @frames=6
        end
        @frame+=1
        if @frame >=@frames
          @currentIndex+=@direction
          @currentIndex=@loop_points[0] if @currentIndex >=@loop_points[1]
          @currentIndex=@loop_points[1]-1 if @currentIndex < @loop_points[0]
          @frame=0
        end
        a = @actualBitmap
        b = @bitmap
        if @__hr_a.equal?(a) && @__hr_b.equal?(b) && @__hr_i == @currentIndex && @__hr_w == @width &&
           @__hr_h == @height && @__hr_s == @scale && b.is_a?(Bitmap) && !b.disposed? &&
           a.__hardrpg_version == @__hr_av && b.__hardrpg_version == @__hr_bv
          return a
        end
        a.clear
        r = a.stretch_blt(Rect.new(0,0,@width,@height),b,Rect.new(@currentIndex*(@width/@scale),0,@width/@scale,@height/@scale))
        if b.is_a?(Bitmap) && !a.disposed?
          @__hr_a = a; @__hr_b = b; @__hr_i = @currentIndex; @__hr_w = @width; @__hr_h = @height; @__hr_s = @scale
          @__hr_av = a.__hardrpg_version; @__hr_bv = b.__hardrpg_version
        else
          @__hr_a = nil
        end
        r
      end
    RUBY

    DPS_UPDATE = <<~'RUBY'
      def update(angle=74)
        return if @lock
        return if !@bitmap || @bitmap.disposed?
        @bitmap.update
        src = @bitmap.bitmap
        sb = @sprite.bitmap
        hb = @shadow.bitmap
        perf = $PokemonSystem.performance
        unless @__hr_src.equal?(src) && @__hr_angle == angle && @__hr_perf == perf &&
               @__hr_sb.equal?(sb) && @__hr_hb.equal?(hb) && src.is_a?(Bitmap) && !src.disposed? &&
               sb.is_a?(Bitmap) && !sb.disposed? && hb.is_a?(Bitmap) && !hb.disposed? &&
               src.__hardrpg_version == @__hr_srcv && sb.__hardrpg_version == @__hr_sbv &&
               hb.__hardrpg_version == @__hr_hbv
          # The sprite shows a copy of src. Refill the copy made last time when it is still the
          # sprite's bitmap and the size matches, instead of a new bitmap per animation frame.
          own = @__hr_own
          if own.is_a?(Bitmap) && !own.disposed? && src.is_a?(Bitmap) && !src.disposed? &&
             sb.equal?(own) && own.width == src.width && own.height == src.height
            own.clear
            own.blt(0, 0, src, src.rect)
            @sprite.bitmap=own
          else
            own = src.clone
            @sprite.bitmap=own
            @__hr_own = own
          end
          if !perf && HardRPGEssentialsNatives.skew_ok?(@shadow)
            # Sprite#skew of a clone of src, done in place: the rows come straight from src (the
            # clone was an exact copy) and the last shadow image is reused when the size matches.
            t = Math.tan(angle*(Math::PI/180))
            w = src.width+((src.height-1)/t)
            h = src.height
            tgt = @__hr_hb
            if tgt.is_a?(Bitmap) && !tgt.disposed? && @shadow.bitmap.equal?(tgt) && tgt.width == w.to_i && tgt.height == h
              tgt.clear
            else
              tgt = Bitmap.new(w,h)
            end
            tgt.__hardrpg_skew_rows(src, t)
            @shadow.bitmap=tgt
          else
            mid = src.clone
            @shadow.bitmap=mid
            @shadow.skew(angle)
            mid.dispose unless @shadow.bitmap.equal?(mid) || mid.disposed?
          end
          sb = @sprite.bitmap
          hb = @shadow.bitmap
          if src.is_a?(Bitmap) && sb.is_a?(Bitmap) && hb.is_a?(Bitmap)
            @__hr_src = src; @__hr_angle = angle; @__hr_perf = perf; @__hr_sb = sb; @__hr_hb = hb
            @__hr_srcv = src.__hardrpg_version; @__hr_sbv = sb.__hardrpg_version; @__hr_hbv = hb.__hardrpg_version
          else
            @__hr_src = nil
          end
        end
          case @status
          when 0
            @sprite.color=Color.new(0,0,0,0)
          when 1 #PSN
            @sprite.color=Color.new(153,102,204,50)
          when 2 #PAR
            @sprite.color=Color.new(255,255,153,50)
          when 3 #FRZ
            @sprite.color=Color.new(153,204,204,50)
          when 4 #BRN
            @sprite.color=Color.new(204,51,51,50)
          end
        @frame+=1
        if @selected==2
          @sprite.visible=(@frame%10<7)
        end
        self.formatShadow
      end
    RUBY

    # Swaps a known LargePlane for HardRPGLargePlane once, right after the section defining it.
    def large_plane
      return false if @large_plane_done || !defined?(LargePlane) || !(LargePlane < Plane)
      file, line = LargePlane.instance_method(:refresh).source_location
      src = file && section_text(file)
      return false unless src
      lines = src.lines
      start = (0...line).to_a.reverse.find { |i| lines[i] =~ /\Aclass LargePlane < Plane\b/ }
      return false unless start
      stop = (start...lines.size).find { |i| lines[i] =~ /\Aend\b/ }
      return false unless stop
      text = lines[start..stop].map { |l| norm(l.chomp) }.reject(&:empty?).join("\n")
      @large_plane_done = true
      return false unless LARGE_PLANE.include?(crc_key(text))
      HardRPGLargePlane.install
      @installed['LargePlane'] = 'engine Plane'
      true
    end
  end
end

# LargePlane on the engine Plane. Same public behaviour: ox/oy keep LargePlane's wrap-around
# adjustment, the plane shows only with a live bitmap, and update does nothing.
module HardRPGLargePlane
  # Engine Plane methods under names LargePlane's own definitions cannot shadow.
  ENGINE = { hardrpg_plane_ox_set: :ox=, hardrpg_plane_oy_set: :oy=, hardrpg_plane_bitmap_set: :bitmap=,
             hardrpg_plane_visible_set: :visible=, hardrpg_plane_zoom_x: :zoom_x,
             hardrpg_plane_zoom_y: :zoom_y, hardrpg_plane_zoom_x_set: :zoom_x=,
             hardrpg_plane_zoom_y_set: :zoom_y= }.freeze
  # LargePlane delegated these to its sprite; the engine Plane has them itself.
  DELEGATED = %i[viewport zoom_x zoom_y opacity blend_type z color tone opacity= blend_type= z= color=
                 tone=].freeze

  BODY = <<~'RUBY'
    def initialize(viewport = nil)
      super(viewport)
      @__disposed = false
      @__ox = 0
      @__oy = 0
      @__bitmap = nil
      @__visible = true
      @borderX = 0
      @borderY = 0
      hardrpg_plane_visible_set(false)
    end

    def disposed?
      @__disposed
    end

    def dispose
      return if @__disposed
      @__bitmap = nil
      @__disposed = true
      super
    end

    def ox; @__ox; end
    def oy; @__oy; end

    def ox=(value)
      if @__ox != value
        @__ox = value
        refresh
      end
    end

    def oy=(value)
      if @__oy != value
        @__oy = value
        refresh
      end
    end

    def bitmap
      @__bitmap
    end

    def bitmap=(value)
      if value.nil?
        unless @__bitmap.nil?
          @__bitmap = nil
          hardrpg_plane_visible_set(false)
        end
      elsif @__bitmap != value && !value.disposed?
        @__bitmap = value
        refresh
      elsif value.disposed?
        unless @__bitmap.nil?
          @__bitmap = nil
          hardrpg_plane_visible_set(false)
        end
      end
    end

    def visible
      @__visible
    end

    def visible=(value)
      @__visible = value
      hardrpg_plane_visible_set(@__visible && !@__bitmap.nil?)
    end

    def zoom_x=(value)
      if hardrpg_plane_zoom_x != value
        hardrpg_plane_zoom_x_set(value)
        refresh
      end
    end

    def zoom_y=(value)
      if hardrpg_plane_zoom_y != value
        hardrpg_plane_zoom_y_set(value)
        refresh
      end
    end

    def update; end

    def refresh
      hardrpg_plane_visible_set(@__visible && !@__bitmap.nil?)
      return unless @__bitmap
      if @__bitmap.disposed?
        hardrpg_plane_visible_set(false)
        return
      end
      zx = hardrpg_plane_zoom_x
      zy = hardrpg_plane_zoom_y
      @__ox += @__bitmap.width * zx if @__ox < 0
      @__oy += @__bitmap.height * zy if @__oy < 0
      @__ox -= @__bitmap.width * zx if @__ox > @__bitmap.width
      @__oy -= @__bitmap.height * zy if @__oy > @__bitmap.height
      begin
        hardrpg_plane_bitmap_set(@__bitmap)
      rescue StandardError
        # Larger than a GPU texture: the engine Plane cannot draw it. Hide rather than stop.
        hardrpg_plane_visible_set(false)
        return
      end
      hardrpg_plane_ox_set(@__ox.to_i)
      hardrpg_plane_oy_set(@__oy.to_i)
    end
  RUBY

  def self.install
    ENGINE.each { |new_name, old| Plane.send(:alias_method, new_name, old) unless Plane.method_defined?(new_name) }
    DELEGATED.each { |m| LargePlane.send(:remove_method, m) if LargePlane.instance_methods(false).include?(m) }
    LargePlane.class_eval(BODY, 'essentials_natives.rb (LargePlane)', 1)
  end
end

if defined?(HardRPGCompat)
  HardRPGCompat.add('Essentials natives') { HardRPGEssentialsNatives.check }
end
