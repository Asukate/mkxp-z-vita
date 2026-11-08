# VX Ace map speedups (preload). Installed at the game's first frame, once every script section has
# run, and only where the game's code is the code each speedup was written for.
#
# Set $hardrpg_vxace_map_speedups = false in a per-game shim to keep every method as the game has it.

# What the script checks found, kept per game (by the scripts' checksum): checking a big game's
# scripts takes 1-3 s on the Vita, and its scripts do not change between plays. Only what the
# scripts say is kept; whether methods are still native is checked on every start.
require 'zlib' unless defined?(Zlib)

module HardRPGVXAceScanCache
  VERSION = 9 # change it whenever a check's result for the same scripts can change
  MAX_GAMES = 64

  @data = nil
  @key = nil
  @dirty = false
  class << self
    def path
      $hardrpg_vxace_scan_cache || 'ux0:/data/hardrpg/cache/vxace-map-speedups.dat'
    end

    def key
      @key = nil unless @key_for.equal?($RGSS_SCRIPTS)
      @key_for = $RGSS_SCRIPTS
      @key ||= begin
        crc = 0
        bytes = 0
        ($RGSS_SCRIPTS || []).each do |section|
          next unless section.is_a?(Array) && section[3].is_a?(String)
          crc = Zlib.crc32(section[3], crc)
          bytes += section[3].bytesize
        end
        "#{VERSION}:#{crc}:#{bytes}"
      end
    end

    def entries
      return @data if @data
      @data = (Marshal.load(File.binread(path)) rescue nil) if path && File.file?(path)
      @data = {} unless @data.is_a?(Hash)
      @data
    end

    # The kept result for name, or the block's result, kept for next time. A check that raised
    # keeps nothing.
    def fetch(name)
      entry = entries[key]
      return entry[name] if entry.is_a?(Hash) && entry.key?(name)
      value = yield
      (entries[key] = entry.is_a?(Hash) ? entry : {})[name] = value
      @dirty = true
      value
    end

    def save
      return unless @dirty && path
      @dirty = false
      data = entries
      data.delete(data.keys.find { |k| k != key }) while data.size > MAX_GAMES
      dir = File.dirname(path)
      [File.dirname(dir), dir].each { |d| Dir.mkdir(d) unless File.directory?(d) }
      File.binwrite("#{path}.tmp", Marshal.dump(data))
      File.rename("#{path}.tmp", path)
    rescue StandardError, NotImplementedError
      nil
    end
  end
end

# Map refresh filter. The default Game_Variables#[]= asks for a map refresh on every write, and a
# refresh checks every page of every event on the map. Clock and HUD scripts write variables every
# frame, so a busy map was refreshed every frame (A New Game: 111 events, a quarter of the frame).
# A refresh can only change which page an event shows, and only a page condition or the game's own
# refresh code reads variables there. So a write asks for a refresh only when the value changed and
# a page condition on the current map reads that variable. Off when the variable or event page code
# is not the default, or when the game's own refresh code mentions variables.
module HardRPGRefreshFilter
  STOCK = {
    'Game_Variables#[]=' => "def []=(variable_id, value)\n@data[variable_id] = value\non_change",
    'Game_Variables#on_change' => "def on_change\n$game_map.need_refresh = true",
    'Game_Event#conditions_met?' => <<~'RUBY'.chomp,
      def conditions_met?(page)
      c = page.condition
      if c.switch1_valid
      return false unless $game_switches[c.switch1_id]
      end
      if c.switch2_valid
      return false unless $game_switches[c.switch2_id]
      end
      if c.variable_valid
      return false if $game_variables[c.variable_id] < c.variable_value
      end
      if c.self_switch_valid
      key = [@map_id, @event.id, c.self_switch_ch]
      return false if $game_self_switches[key] != true
      end
      if c.item_valid
      item = $data_items[c.item_id]
      return false unless $game_party.has_item?(item)
      end
      if c.actor_valid
      actor = $game_actors[c.actor_id]
      return false unless $game_party.members.include?(actor)
      end
      return true
    RUBY
  }.freeze
  # Methods a refresh runs. The game's own versions of these, and of whatever they call, must not
  # read variables.
  REFRESH_PATH = %w[Game_Map#refresh Game_Map#refresh_tile_events Game_Event#refresh Game_Event#find_proper_page
                    Game_Event#setup_page Game_Event#setup_page_settings Game_Event#clear_page_settings
                    Game_CommonEvent#refresh Game_CommonEvent#active?].freeze
  # Default text of the refresh path, so that only a game's changes are searched.
  REFRESH_STOCK = {
    'Game_Map#refresh' => "def refresh\n@events.each_value {|event| event.refresh }\n" \
                          "@common_events.each {|event| event.refresh }\nrefresh_tile_events\n@need_refresh = false",
    'Game_Map#refresh_tile_events' => "def refresh_tile_events\n@tile_events = @events.values.select {|event| event.tile? }",
    'Game_Event#refresh' => "def refresh\nnew_page = @erased ? nil : find_proper_page\n" \
                            "setup_page(new_page) if !new_page || new_page != @page",
    'Game_Event#find_proper_page' => "def find_proper_page\n@event.pages.reverse.find {|page| conditions_met?(page) }",
    'Game_CommonEvent#refresh' => "def refresh\nif active?\n@interpreter ||= Game_Interpreter.new\nelse\n@interpreter = nil\nend",
    'Game_CommonEvent#active?' => "def active?\n@event.parallel? && $game_switches[@event.switch_id]",
  }.freeze
  # Classes whose methods a refresh can reach (RPG::Event reads as "RPG" to the normalizer).
  SCAN_CLASSES = %w[Game_Map Game_Event Game_Character Game_CharacterBase Game_CommonEvent RPG].freeze

  @status = nil
  @key = nil
  @ids = nil

  class << self
    attr_reader :status

    # {"Class#name" => [[text, section index], ...]} for the given classes, from the script sections
    # that open one of them. Parsing every section of a big game takes seconds on the Vita (BLACK
    # SOULS II: 224 sections, ~5 s), so only those sections are parsed, each at most once.
    def definitions(scripts, classes = nil)
      @parsed = {} unless @parsed_for.equal?(scripts)
      @parsed_for = scripts
      pattern = classes && /\b(?:class|module)\s+(?:::)?(?:#{classes.map { |c| Regexp.escape(c) }.join('|')})\b/
      out = Hash.new { |h, k| h[k] = [] }
      scripts.each_with_index do |section, i|
        next unless section.is_a?(Array) && section[3].is_a?(String)
        text = section[3]
        next unless text.include?('def ') && (pattern.nil? || text.match?(pattern))
        methods = (@parsed[i] ||= HardRPGNative::Norm.methods(text))
        methods.each do |key, (body, _)|
          next if classes && !classes.include?(key.split('#', 2)[0])
          out[key] << [body, i]
        end
      end
      out
    end

    # keys: the stock methods to compare; path: also search the game's refresh code.
    def check(defs, keys = STOCK.keys, path = keys.size == STOCK.size)
      keys.each do |key|
        list = defs[key]
        return "off (no #{key})" if list.empty?
        return "off (custom #{key})" unless list.all? { |t, _| t == STOCK[key] }
      end
      return 'on' unless path
      # The game's own refresh code, and every method of the same name as something it calls.
      seen = {}
      queue = REFRESH_PATH.map { |k| k.split('#', 2)[1] }
      until queue.empty?
        name = queue.shift
        next if seen[name]
        seen[name] = true
        defs.each do |key, list|
          next unless key.end_with?("##{name}") && SCAN_CLASSES.include?(key.split('#', 2)[0])
          list.each do |text, _|
            next if REFRESH_STOCK[key] == text || STOCK[key] == text
            return "off (#{key} reads variables)" if text =~ /variable/i
            text.scan(/(?<![@$:\w])([a-z_]\w*[?!]?)/) { |(word)| queue << word unless seen[word] }
          end
        end
      end
      'on'
    end

    def install
      return @status if @status
      return @status = 'off (disabled)' if $hardrpg_vxace_map_speedups == false
      return @status = 'off (not VX Ace)' unless Kernel.respond_to?(:rgss_main, true)
      return @status = 'off (no native events)' unless defined?(HardRPGNative::Norm)
      return @status = 'off (no Game_Variables)' unless defined?(Game_Variables) && defined?(Game_Event)
      scripts = $RGSS_SCRIPTS || []
      @status = HardRPGVXAceScanCache.fetch(:refresh_filter) do
        status = check(definitions(scripts, %w[Game_Variables]), STOCK.keys.grep(/\AGame_Variables#/))
        status = check(definitions(scripts, %w[Game_Event]), ['Game_Event#conditions_met?'], false) if status == 'on'
        status = check(definitions(scripts, SCAN_CLASSES + %w[Game_Variables])) if status == 'on'
        status
      end
      return @status unless @status == 'on'
      Game_Variables.class_eval do
        def []=(variable_id, value)
          old = @data[variable_id]
          @data[variable_id] = value
          on_change if HardRPGRefreshFilter.refresh?(variable_id, old, value)
        end
      end
      @status
    rescue StandardError => e
      @status = "off (#{e.class})"
    end

    # True when this write can change an event's page on the current map.
    def refresh?(id, old, value)
      return true if $hardrpg_refresh_filter_off
      return false if old == value && (value.is_a?(Integer) || value.nil?)
      ids = page_condition_ids
      ids.nil? || ids[id]
    end

    # Variable ids read by a page condition of an event on the map now set up; nil when unknown.
    def page_condition_ids
      map = $game_map
      return nil unless map
      events = map.instance_variable_get(:@events)
      return nil unless events.is_a?(Hash)
      key = [map.map_id, events.object_id, events.size]
      return @ids if key == @key
      ids = {}
      events.each_value do |ev|
        data = ev.instance_variable_get(:@event)
        return nil unless data.respond_to?(:pages)
        data.pages.each do |page|
          c = page.condition
          ids[c.variable_id] = true if c.variable_valid
        end
      end
      @key = key
      @ids = ids
    end
  end
end

# KNS "animation character chips" ({N} in a character file name): its Sprite_Character#update_src_rect
# matched the file name against a regular expression for every character sprite on every frame (a
# third of the frame in A New Game). The same method, with the match kept until the name changes.
module HardRPGAnimeChipSpeedup
  KNS = <<~'RUBY'.chomp
    def update_src_rect
    if @character_name && @character_name =~ /\{(\d+)\}/
    index = @character.anime_frame %= $1.to_i
    sx, sy = (index % 6) * @cw, (index / 6) * @ch
    self.src_rect.set(sx, sy, @cw, @ch)
    else
    kns_update_src_rect
    end
  RUBY

  @status = nil
  class << self
    attr_reader :status

    def install
      return @status if @status
      return @status = 'off (disabled)' if $hardrpg_vxace_map_speedups == false
      return @status = 'off (no script)' unless defined?(Sprite_Character) && defined?(HardRPGNative::Norm)
      um = (Sprite_Character.instance_method(:update_src_rect) rescue nil)
      return @status = 'off (no script)' unless um && um.owner == Sprite_Character && um.source_location
      return @status = 'off (no script)' unless Sprite_Character.method_defined?(:kns_update_src_rect)
      file, line = um.source_location
      index = file.to_s[/\A\{(\d+)\}\z/, 1] || file.to_s[/\A(\d+):/, 1]
      section = index && ($RGSS_SCRIPTS || [])[index.to_i]
      return @status = 'off (no script)' unless section.is_a?(Array) && section[3].is_a?(String)
      text, at = HardRPGNative::Norm.methods(section[3])['Sprite_Character#update_src_rect']
      return @status = 'off (other script)' unless text == KNS && at == line
      Sprite_Character.class_eval do
        def update_src_rect
          name = @character_name
          if name
            if name != @hardrpg_kns_name
              @hardrpg_kns_name = name.dup
              @hardrpg_kns_frames = name =~ /\{(\d+)\}/ ? $1.to_i : nil
            end
            frames = @hardrpg_kns_frames
          end
          if frames
            index = @character.anime_frame %= frames
            sx, sy = (index % 6) * @cw, (index / 6) * @ch
            self.src_rect.set(sx, sy, @cw, @ch)
          else
            kns_update_src_rect
          end
        end
      end
      @status = 'on'
    rescue StandardError => e
      @status = "off (#{e.class})"
    end
  end
end

# Unchanged switch writes. The default Game_Switches#[]= and Game_SelfSwitches#[]= ask for a map
# refresh on every write, also when the switch already had that value. Distance and proximity scripts
# set self switches for every event on every frame (A New Game: a refresh every frame). Writing the
# same true, false or nil again now changes nothing, so it asks for nothing; any other write is as
# before. Off for a class whose []= is not the default.
module HardRPGSwitchSkip
  STOCK = {
    'Game_Switches' => "def []=(switch_id, value)\n@data[switch_id] = value\non_change",
    'Game_SelfSwitches' => "def []=(key, value)\n@data[key] = value\non_change",
  }.freeze

  @status = nil
  class << self
    attr_reader :status

    def check(defs, cname)
      list = defs["#{cname}#[]="]
      return 'off (none)' if list.empty?
      return 'off (custom)' unless list.all? { |t, _| t == STOCK[cname] }
      'on'
    end

    def same?(old, value)
      (value.nil? || value == true || value == false) && old.equal?(value)
    end

    def install
      return @status if @status
      return @status = 'off (disabled)' if $hardrpg_vxace_map_speedups == false
      return @status = 'off (not VX Ace)' unless Kernel.respond_to?(:rgss_main, true)
      return @status = 'off (no native events)' unless defined?(HardRPGNative::Norm)
      found = HardRPGVXAceScanCache.fetch(:switch_skip) do
        defs = HardRPGRefreshFilter.definitions($RGSS_SCRIPTS || [], STOCK.keys)
        STOCK.keys.map { |cname| [cname, check(defs, cname)] }.to_h
      end
      out = {}
      STOCK.each_key do |cname|
        next out[cname] = 'off (none)' unless Object.const_defined?(cname)
        out[cname] = found[cname] || 'off (none)'
        next unless out[cname] == 'on'
        # Crash rescue wraps Game_SelfSwitches methods to see changes; a changed value still calls
        # the wrapped on_change, and an unchanged one changes nothing to save.
        Object.const_get(cname).class_eval do
          def []=(key, value)
            old = @data[key]
            @data[key] = value
            on_change unless HardRPGSwitchSkip.same?(old, value)
          end
        end
      end
      @status = out.map { |k, v| "#{k} #{v}" }.join(', ')
    rescue StandardError => e
      @status = "off (#{e.class})"
    end
  end
end

# Far off-screen character sprites. With the native Spriteset_Map#update_characters and
# Sprite_Character#update, a sprite that was drawn, and whose character now is, more than a whole
# screen away is not updated (it would draw nothing), unless it has an animation or a balloon. Off
# when a script changes a Sprite_Character method that could do more than pick its picture or place.
module HardRPGOffscreenSprites
  # Methods a game may change and keep this on: they only choose what a visible sprite shows.
  # dispose: an update never calls it.
  SAFE = %w[set_character_bitmap update_src_rect update_position move_animation update_bitmap
            graphic_changed? set_tile_bitmap tileset_bitmap note dispose].freeze
  # Known script texts of other methods that only copy the character's look onto the sprite.
  KNOWN = {
    # Hime's Character Sprite Zoom: original update_other, then zoom_x/zoom_y from the character.
    'update_other' => %w[2696220213:2716806441:118],
  }.freeze
  # ...each over this default text.
  KNOWN_BASE = { 'update_other' => '800444980:3104521863:169' }.freeze

  @status = nil
  class << self
    attr_reader :status

    def check(defs, swapped)
      %w[Spriteset_Map#update_characters Sprite_Character#update].each do |key|
        return "off (#{key} not native)" unless swapped.include?(key)
      end
      check_scripts(defs)
    end

    # Defs the normalizer does not read (not indented one or two spaces or a tab under their class
    # line) are invisible to check_scripts. A def like that under `class Sprite_Character` keeps this
    # off, except in Hudell's Character Effects, whose effects the native skip checks for itself.
    def check_unreadable(scripts)
      shake = HardRPGSpriteScripts.sections(scripts)[:shake]
      guard = false
      scripts.each_with_index do |section, i|
        next unless section.is_a?(Array) && (text = section[3]).is_a?(String)
        next unless text.match?(/\bclass\s+(?:::)?Sprite_Character\b/)
        next unless unread_sprite_def?(text)
        return "off (unread Sprite_Character def in section #{i})" unless i == shake
        guard = true
      end
      guard ? 'on (hudell guard)' : 'on'
    end

    def unread_sprite_def?(text)
      klass = nil
      indent = ''
      text.each_line do |line|
        if (m = line.match(/\A([ \t]*)(?:class|module)\s+(?:::)?([\w:<]+)/))
          indent = m[1]
          klass = m[2]
        elsif klass == 'Sprite_Character' && (m = line.match(/\A([ \t]*)def\s/))
          inner = m[1]
          return true unless inner.start_with?(indent) && inner[indent.size..].match?(/\A( {1,2}|\t)\z/)
        end
      end
      false
    end

    def check_scripts(defs)
      defs.each do |key, list|
        cname, name = key.split('#', 2)
        next unless cname == 'Sprite_Character' && !SAFE.include?(name)
        next if list.size <= 1
        known = KNOWN[name] || []
        next if HardRPGKnownMethods.digest(list[0][0]) == KNOWN_BASE[name] &&
                list.drop(1).all? { |text, _| known.include?(HardRPGKnownMethods.digest(text)) }
        return "off (custom Sprite_Character##{name})"
      end
      'on'
    end

    # The character classes whose origin_x, origin_y and flash_duration are Hudell's own readers
    # (lines 62, 69 and 93 of its section): the parking check may read their variables directly.
    def hudell_direct_classes
      shake = HardRPGVXAceScanCache.fetch(:sprite_scripts) { HardRPGSpriteScripts.sections }[:shake]
      return [] unless shake
      %w[Game_Event Game_Player Game_Follower Game_Vehicle].select do |cname|
        Object.const_defined?(cname) &&
          [['origin_x', 62], ['origin_y', 69], ['flash_duration', 93]].all? do |name, line|
            HardRPGSpriteScripts.at?(shake, cname, name, line)
          end
      end.map { |cname| Object.const_get(cname) }
    rescue StandardError
      []
    end

    def install
      return @status if @status
      return @status = 'off (disabled)' if $hardrpg_vxace_map_speedups == false
      return @status = 'off (not VX Ace)' unless Kernel.respond_to?(:rgss_main, true)
      return @status = 'off (no native events)' unless defined?(HardRPGNative) && HardRPGNative.respond_to?(:set_option)
      swapped = HardRPGNative.respond_to?(:swapped) ? HardRPGNative.swapped : []
      %w[Spriteset_Map#update_characters Sprite_Character#update].each do |key|
        cname, name = key.split('#', 2)
        um = (Object.const_get(cname).instance_method(name) rescue nil)
        return @status = "off (#{key} replaced later)" unless um && um.source_location.nil?
      end
      %w[Spriteset_Map#update_characters Sprite_Character#update].each do |key|
        return @status = "off (#{key} not native)" unless swapped.include?(key)
      end
      @status = HardRPGVXAceScanCache.fetch(:offscreen) do
        status = check_scripts(HardRPGRefreshFilter.definitions($RGSS_SCRIPTS || [], %w[Sprite_Character]))
        status == 'on' ? check_unreadable($RGSS_SCRIPTS || []) : status
      end
      if @status.start_with?('on')
        if @status.include?('hudell')
          HardRPGNative.set_option('park_guard_hudell', true)
          HardRPGNative.set_option('hudell_direct', hudell_direct_classes)
        end
        HardRPGNative.set_option('offscreen_skip', true)
      end
      @status
    rescue StandardError => e
      @status = "off (#{e.class})"
    end
  end
end

# Database load without garbage collection. DataManager.load_database reads a dozen files that are
# almost all objects the game keeps; collecting while they load only marks them again and again
# (BLACK SOULS II: 8 collections, 1.4 s of an 8.1 s load on the Vita). The engine already pauses
# collection for files of 1 MB and up; this covers the smaller ones and the gaps between them.
# Popular per-sprite scripts with native versions (binding/sprite-scripts.cpp). Each script is matched by
# its whole section text, and every method the native module replaces must still be that text's.
module HardRPGSpriteScripts
  # name => [bytes, crc32, adler32] of the script section
  SECTIONS = {
    shake: [3288, 2996714353, 807731552],   # Hudell's Character Effects 1.5
    frames: [13900, 3333268617, 3104299944], # Modern Algebra's Extra Movement Frames 1.0.1
    tall: [6602, 3926983123, 1963855639],    # Tall Sprite Fix (Neon Black)
    zoom: [7436, 2253961055, 4282459025],    # Hime's Character Sprite Zoom
  }.freeze
  # module => [[section, class, method, line], ...] that must resolve to that section and line
  NEEDS = {
    TallSprite: [[:tall, 'Sprite_Character', 'update_bitmap', 38], [:tall, 'Sprite_Character', 'update_position', 49],
                 [:tall, 'Sprite_Character', 'sprite_is_onscreen?', 69], [:tall, 'Sprite_Character', 'get_edge_corner_dis', 82],
                 [:tall, 'Sprite_Character', 'check_encompassed_area', 89], [:tall, 'Game_Event', 'large_sprite', 151]],
    ExtraFrames: [[:frames, 'Sprite_Character', 'update_src_rect', 257]],
    ShakeZoom: [[:zoom, 'Sprite_Character', 'update_other', 121],
                [:shake, 'Sprite_Character', 'th_character_sprite_zoom_update_other', 118],
                [:shake, 'Sprite_Character', 'update_flash', 127], [:shake, 'Sprite_Character', 'update_origin', 134],
                [:shake, 'Game_CharacterBase', 'origin_x', 62], [:shake, 'Game_CharacterBase', 'origin_y', 69]],
  }.freeze

  @status = nil
  class << self
    attr_reader :status

    # {name => section index} of the matching sections.
    def sections(scripts = $RGSS_SCRIPTS || [])
      by_size = SECTIONS.map { |name, (bytes, _, _)| [bytes, name] }.to_h
      found = {}
      scripts.each_with_index do |section, i|
        next unless section.is_a?(Array) && section[3].is_a?(String)
        name = by_size[section[3].bytesize]
        next unless name
        _, crc, adler = SECTIONS[name]
        found[name] = i if Zlib.crc32(section[3]) == crc && Zlib.adler32(section[3]) == adler
      end
      found
    end

    # Galv's Character Effects: Sprite_Shadow's per-frame methods, each its exact text (the section's
    # settings differ per game, so it is matched method by method).
    GALV = { 'Sprite_Shadow#update_position' => '641374800:322576569:92',
             'Sprite_Shadow#get_angle' => '371233615:1491301954:358',
             'Sprite_Shadow#update_facing' => '341681818:3950914137:118',
             'Sprite_Shadow#update_other' => '2782869051:2586846037:95' }.freeze

    # Sprite_Shadow#update is Galv's: the sprite steps, then the shadow's own (its lines compared).
    GALV_UPDATE = ['def update', 'super', 'update_bitmap', 'update_src_rect', 'update_position', 'update_other',
                   'update_facing', 'end'].freeze
    def galv_update?
      um = Sprite_Shadow.instance_method(:update)
      loc = um.owner.equal?(Sprite_Shadow) && um.source_location
      found = loc && HardRPGKnownMethods.section_of(loc[0])
      lines = found && found[1].lines[loc[1] - 1, GALV_UPDATE.size]
      !!lines && lines.map(&:strip) == GALV_UPDATE
    rescue StandardError
      false
    end

    def galv_shadow?
      return false unless defined?(Sprite_Shadow) && Sprite_Shadow < Sprite_Character &&
                          defined?(HardRPGNative::SpriteScripts::GalvShadow)
      HardRPGVXAceScanCache.fetch(:galv_shadow) { !HardRPGKnownMethods.locate(GALV).nil? }
    end

    CHARACTERS = %w[Game_Event Game_Player Game_Follower Game_Vehicle].freeze

    # Hime's zoom: Game_Character#update and update_zoom are the section's (lines 154 and 159) for
    # every character class, and the alias it calls exists.
    def zoom_update?(found)
      return false unless found[:zoom] && defined?(HardRPGNative::SpriteScripts::ZoomUpdate)
      at?(found[:zoom], 'Game_Character', 'update', 154) &&
        Game_Character.method_defined?(:th_character_sprite_zooming_update, true) &&
        CHARACTERS.all? do |c|
          !Object.const_defined?(c) || (Object.const_get(c).instance_method(:update_zoom).source_location ==
                                        Game_Character.instance_method(:update_zoom).source_location &&
                                        at?(found[:zoom], c, 'update_zoom', 159))
        end
    rescue StandardError
      false
    end

    # Sound Emitting Events: Game_Event#update is exactly this wrapper (its section's other code is
    # not read by the normalizer, so the method's own lines are compared).
    MADSEE = ['def update(*args)', 'madsee_updt_1ex0(*args) # Call original method',
              '@madsee_emitter.update if @madsee_emitter', 'end'].freeze
    def madsee_update?
      return false unless defined?(Game_Event) && defined?(HardRPGNative::SpriteScripts::MadseeUpdate)
      um = Game_Event.instance_method(:update)
      loc = um.owner.equal?(Game_Event) && um.source_location
      found = loc && HardRPGKnownMethods.section_of(loc[0])
      return false unless found && Game_Event.method_defined?(:madsee_updt_1ex0, true)
      lines = found[1].lines[loc[1] - 1, MADSEE.size]
      lines && lines.map(&:strip) == MADSEE
    rescue StandardError
      false
    end

    def at?(index, cname, name, line)
      um = (Object.const_get(cname).instance_method(name) rescue nil)
      loc = um && um.source_location
      found = loc && HardRPGKnownMethods.section_of(loc[0])
      found && found[0] == index && loc[1] == line
    end

    def install
      return @status if @status
      return @status = 'off (disabled)' if $hardrpg_vxace_map_speedups == false || $hardrpg_sprite_scripts == false
      return @status = 'off (no natives)' unless defined?(HardRPGNative::SpriteScripts) && defined?(Sprite_Character)
      found = HardRPGVXAceScanCache.fetch(:sprite_scripts) { sections }
      on = NEEDS.select do |mod, list|
        list.all? { |sec, cname, name, line| found[sec] && at?(found[sec], cname, name, line) }
      end.keys
      galv = galv_shadow?
      return @status = 'off (no known scripts)' if on.empty? && !galv && !zoom_update?(found) && !madsee_update?
      HardRPGNative::SpriteScripts.bind(Sprite_Character)
      on.each { |mod| Sprite_Character.prepend(HardRPGNative::SpriteScripts.const_get(mod)) }
      if galv
        Sprite_Shadow.prepend(HardRPGNative::SpriteScripts::GalvShadow)
        on << :GalvShadow
        if galv_update? && HardRPGOffscreenSprites.status.to_s.start_with?('on')
          Sprite_Shadow.prepend(HardRPGNative::SpriteScripts::GalvShadowPark)
          HardRPGNative.set_option('shadow_park', true)
          on << :GalvShadowPark
        end
      end
      if zoom_update?(found)
        Game_Character.prepend(HardRPGNative::SpriteScripts::ZoomUpdate)
        on << :ZoomUpdate
      end
      if madsee_update?
        Game_Event.prepend(HardRPGNative::SpriteScripts::MadseeUpdate)
        on << :MadseeUpdate
      end
      @status = "on (#{on.join(' ')})"
    rescue StandardError => e
      @status = "off (#{e.class})"
    end
  end
end

# Unchanged character sprites (binding/native-events.cpp, option steady_skip). A sprite whose last
# update changed nothing, and nothing that update reads has changed since, is not updated again. That
# holds only while every method a no-change update runs reads nothing but the sprite, the character
# instance variables in CHAR_IVARS (plus the known scripts' own), the map's scroll position and tiles
# and the screen size. So each such method the game resolves now must be native, an attribute of
# those variables, the default text, or a method of a known sprite script whose native module is on.
module HardRPGSteadySprites
  CHAR_CLASSES = %w[Game_Event Game_Player Game_Follower].freeze
  CHAR_IVARS = %w[@real_x @real_y @x @y @priority_type @jump_peak @jump_count @tile_id @character_name
                  @character_index @pattern @direction @opacity @blend_type @bush_depth @transparent
                  @animation_id @balloon_id].freeze
  # Instance variables each known sprite script's methods read from the character.
  SECTION_IVARS = { shake: %w[@origin_x @origin_y @angle @flash_duration @flash_color],
                    zoom: %w[@zoom_x @zoom_y], tall: %w[@large_sprite], frames: [] }.freeze
  MODULE_SECTIONS = { 'TallSprite' => %i[tall], 'ExtraFrames' => %i[frames], 'ShakeZoom' => %i[shake zoom] }.freeze
  # What an update that changes nothing calls (the steps that run only on a change, such as
  # set_character_bitmap or start_balloon, change the sprite: such an update is never skipped).
  SPRITE_NAMES = %w[update update_bitmap graphic_changed? update_src_rect update_position move_animation
                    update_other update_balloon setup_new_effect animation? update_animation
                    sprite_is_onscreen? check_encompassed_area get_edge_corner_dis update_origin
                    hudell_update_other th_character_sprite_zoom_update_other cp_011214_update_bitmap
                    cp_073013_update_pos maemf_updtsrcrect_2kq5 ma_update_emf_src_rect ma_char_is_emf_sprite?
                    character x y z ox oy width height visible opacity src_rect bitmap viewport
                    x= y= z= ox= oy= visible= opacity= blend_type= bush_depth= zoom_x= zoom_y= angle=
                    is_a?].freeze
  CHAR_NAMES = %w[tile_id character_name character_index pattern direction opacity blend_type bush_depth
                  transparent animation_id balloon_id screen_x screen_y screen_z jump_height shift_y
                  object_character? real_x real_y x y origin_x origin_y angle flash_duration zoom_x zoom_y
                  large_sprite is_a?].freeze
  MAP_NAMES = %w[adjust_x adjust_y display_x display_y terrain_tag loop_horizontal? loop_vertical? valid?
                 layered_tiles tileset tile_id width height screen_tile_x screen_tile_y].freeze
  MAP_IVARS = %w[@display_x @display_y].freeze
  # Default texts HardRPGNative::STOCK does not list. (Tiles are read from the map's data and the
  # tileset's flags, which no script changes in place; the native side checks the objects.)
  EXTRA = {
    'Game_Map#terrain_tag' => '3380447795:4215092644:158',
    'Game_Map#valid?' => '4029894196:4133621796:60',
    'Game_Map#layered_tiles' => '3802473541:3432518973:65',
    'Game_Map#tileset' => '3363835930:610275068:39',
    'Game_Map#tile_id' => '367210931:1138232817:44',
  }.freeze
  STOCK_TAGS = ['', 'jp', 'en', 'sp1'].freeze

  @status = nil
  @locs = nil
  class << self
    attr_reader :status

    def stock?(key, text)
      digest = HardRPGKnownMethods.digest(text)
      return true if EXTRA[key] == digest
      stock = defined?(HardRPGNative::STOCK) ? HardRPGNative::STOCK : {}
      STOCK_TAGS.include?(stock["#{key}:#{digest}"])
    end

    # nil when klass#name may run in a skipped update, else why not.
    def refuse(klass, name, allowed, ivars, swapped, parsed)
      um = (klass.instance_method(name) rescue nil)
      return nil unless um # never called: it would raise
      owner = um.owner
      key = "#{owner.name}##{name}"
      loc = um.source_location
      unless loc
        # an alias keeps the name it was defined under (hudell_update_other: the native update_other)
        return nil if [Kernel, Object, BasicObject, Sprite].include?(owner) || swapped.include?(key) ||
                      swapped.include?("#{owner.name}##{um.original_name}")
        return nil if owner.name.to_s.start_with?('HardRPGNative::SpriteScripts::')
        return "native #{key}"
      end
      index, src = HardRPGKnownMethods.section_of(loc[0])
      return "#{key} outside the scripts" unless src
      return nil if allowed.include?(index)
      if RubyVM::InstructionSequence.of(um).nil? # attr_reader / attr_writer
        return nil if ivars.nil? || ivars.include?("@#{name.to_s.chomp('=')}")
        return "attribute #{key}"
      end
      text, line = (parsed[index] ||= HardRPGNative::Norm.methods(src))[key]
      return nil if text && line == loc[1] && stock?(key, text)
      "custom #{key}"
    end

    def check(found, modules)
      allowed = modules.flat_map { |m| MODULE_SECTIONS[m] || [] }.map { |sec| found[sec] }.compact
      ivars = CHAR_IVARS + modules.flat_map { |m| MODULE_SECTIONS[m] || [] }.flat_map { |sec| SECTION_IVARS[sec] }
      swapped = HardRPGNative.respond_to?(:swapped) ? HardRPGNative.swapped : []
      parsed = {}
      groups = [[Sprite_Character, SPRITE_NAMES, nil]] +
               CHAR_CLASSES.map { |c| [Object.const_get(c), CHAR_NAMES, ivars] } +
               [[Game_Map, MAP_NAMES, MAP_IVARS]]
      groups.each do |klass, names, iv|
        names.each do |name|
          why = refuse(klass, name, allowed, iv, swapped, parsed)
          return ["off (#{why})", nil] if why
        end
      end
      ['on', ivars]
    end

    # Where each checked method resolves now.
    def resolutions
      groups = [[Sprite_Character, SPRITE_NAMES]] + CHAR_CLASSES.map { |c| [Object.const_get(c), CHAR_NAMES] } +
               [[Game_Map, MAP_NAMES]]
      groups.flat_map do |klass, names|
        names.map do |n|
          um = (klass.instance_method(n) rescue nil)
          um && [um.owner, um.source_location]
        end
      end + [Sprite_Character.ancestors.size]
    end

    # Called by the native loop after any method definition: are the methods still the ones checked?
    def steady_ok?
      @locs && resolutions == @locs
    rescue StandardError
      false
    end

    def install
      return @status if @status
      return @status = 'off (disabled)' if $hardrpg_vxace_map_speedups == false || $hardrpg_steady_sprites == false
      return @status = 'off (no natives)' unless HardRPGNative.respond_to?(:steady_setup)
      off = HardRPGOffscreenSprites.status.to_s
      return @status = "off (offscreen #{off})" unless off.start_with?('on')
      sprite = HardRPGSpriteScripts.status.to_s
      modules = sprite.start_with?('on') ? sprite[/\((.*)\)/, 1].to_s.split : []
      return @status = 'off (hudell without its native)' if off.include?('hudell') && !modules.include?('ShakeZoom')
      return @status = 'off (no Game_Map)' unless defined?(Game_Map) && CHAR_CLASSES.all? { |c| Object.const_defined?(c) }
      found = HardRPGVXAceScanCache.fetch(:sprite_scripts) { HardRPGSpriteScripts.sections }
      status, ivars = HardRPGVXAceScanCache.fetch(:steady) { check(found, modules) }
      return @status = status unless status == 'on'
      @locs = resolutions
      HardRPGNative.steady_setup(CHAR_CLASSES.map { |c| Object.const_get(c) }, ivars)
      HardRPGNative.set_option('steady_skip', true)
      @status = 'on'
    rescue StandardError => e
      @status = "off (#{e.class})"
    end
  end
end

module HardRPGDatabaseLoad
  def self.wrap
    return if @wrapped || $hardrpg_vxace_map_speedups == false
    return unless defined?(DataManager) && DataManager.respond_to?(:load_database)
    @wrapped = true
    DataManager.singleton_class.prepend(Module.new do
      def load_database(*args, &block)
        was_off = GC.disable
        super
      ensure
        GC.enable unless was_off
      end
    end)
  end
end

# Known method texts. A speedup that relies on a method being the default (or a known script's) text
# checks that the method a class resolves now was defined by a section holding exactly that text.
module HardRPGKnownMethods
  class << self
    def digest(text)
      "#{Zlib.crc32(text)}:#{Zlib.adler32(text)}:#{text.bytesize}"
    end

    def section_of(file)
      index = file.to_s[/\A\{(\d+)\}\z/, 1] || file.to_s[/\A(\d+):/, 1]
      section = index && ($RGSS_SCRIPTS || [])[index.to_i]
      section.is_a?(Array) && section[3].is_a?(String) ? [index.to_i, section[3]] : nil
    end

    # {"Class#name" => [file, line]} when each method in want ({"Class#name" => digest or digests})
    # is now that text, or nil. Digests come from HardRPGNative::Norm, as in native_events.rb.
    def locate(want)
      parsed = {}
      out = {}
      want.each do |key, digests|
        cname, name = key.split('#', 2)
        return nil unless Object.const_defined?(cname)
        klass = Object.const_get(cname)
        um = (klass.instance_method(name) rescue nil)
        loc = um && um.owner.equal?(klass) && um.source_location
        return nil unless loc
        index, src = section_of(loc[0])
        return nil unless src
        text, line = (parsed[index] ||= HardRPGNative::Norm.methods(src))[key]
        return nil unless text && line == loc[1] && Array(digests).include?(digest(text))
        out[key] = loc
      end
      out
    end

    # The methods found by locate are still the ones in place.
    def same?(locs)
      locs.all? do |key, loc|
        cname, name = key.split('#', 2)
        (Object.const_get(cname).instance_method(name).source_location rescue nil) == loc
      end
    end
  end
end

# Jet10985's pathfinder with Venima's changes ("Pathfinding & Event formations"), used by many VX
# Ace games for chasers: every step of a chase runs a whole search from the chaser to the player.
# Its open and closed lists are arrays searched one node at a time, so a search that gives up after
# 1000 nodes made millions of Ruby calls (Ao Oni's Mines: 0.65 s). This is the same search with
# hash lookups: the same nodes in the same order, the same passable? calls, the same route (built
# by the game's own recreate_path). While a search runs, the map also answers events_xy_nt from a
# position index and keeps each tile's passage answer; nothing on the map moves during a search.
module HardRPGJetPath
  SEARCH = {
    'Game_Map#find_path' => '2400997801:1770213618:981',
    'Game_Map#each_neighbor' => '2944310640:2801426332:285',
    'Node#initialize' => '125828196:1832460518:97',
    'Node#mark_path' => '20748930:1743394057:58',
    'Node#total_cost' => '2998039965:4148432274:36',
    'Node#==' => '1777592114:3266513829:34',
    'Point#initialize' => '1722425002:3386641041:34',
    'Point#==' => '2464398210:1049369115:80',
    'Point#distance' => '1806895824:1164841673:59',
    'Game_Map#round_x' => '3717177633:1181881102:57',
    'Game_Map#round_y' => '2321096418:1017254612:57',
    'Game_Map#round_x_with_direction' => '3260917369:3740472808:76',
    'Game_Map#round_y_with_direction' => '3040158680:3749320172:76',
  }.freeze
  # The event index stands in for the default events_xy_nt (default pos_nt? and pos?).
  INDEX = {
    'Game_Map#events_xy_nt' => '2271787243:574692209:75',
    'Game_CharacterBase#pos_nt?' => '1977972506:296422599:41',
    'Game_CharacterBase#pos?' => '1023083197:2722236635:33',
  }.freeze
  # A tile's passage depends only on the map's tiles and its tile events.
  MEMO = {
    'Game_Map#passable?' => '516777856:3980661519:69',
    'Game_Map#check_passage' => '973991880:1095123498:199',
    'Game_Map#all_tiles' => '2025312439:2612010914:89',
    'Game_Map#layered_tiles' => '3802473541:3432518973:65',
    'Game_Map#tile_id' => '367210931:1138232817:44',
    'Game_Map#tile_events_xy' => '937110794:678566796:75',
    'Game_Map#tileset' => '3363835930:610275068:39',
  }.freeze
  # Node#<=> is an operator method the normalizer does not read: its three lines, checked directly.
  COMPARE = ['def <=>(other)', 'total_cost <=> other.total_cost', 'end'].freeze
  W = 1 << 16 # node key: x * W + y, for map coordinates

  @status = nil
  @mode = 7 # 1: search, 2: event index, 4: passage memo (tests switch them)
  class << self
    attr_reader :status, :pos_ok
    attr_accessor :mode

    def compare_ok?
      um = (Node.instance_method(:<=>) rescue nil)
      loc = um && um.owner.equal?(Node) && um.source_location
      _, src = loc && HardRPGKnownMethods.section_of(loc[0])
      return false unless src
      lines = src.lines[loc[1] - 1, 3] || []
      lines.map { |l| HardRPGNative::Norm.norm_line(l.chomp) } == COMPARE
    end

    def install
      return @status if @status
      return @status = 'off (disabled)' if $hardrpg_vxace_map_speedups == false || $hardrpg_jet_path == false
      return @status = 'off (no script)' unless defined?(Jet::Pathfinder) && defined?(Node) && defined?(Point) &&
                                                defined?(Game_Map) && defined?(HardRPGNative::Norm)
      found = HardRPGVXAceScanCache.fetch(:jet_path) do
        search = HardRPGKnownMethods.locate(SEARCH)
        search && compare_ok? ? [search, HardRPGKnownMethods.locate(INDEX), HardRPGKnownMethods.locate(MEMO)] : nil
      end
      return @status = 'off (other script)' unless found && HardRPGKnownMethods.same?(found[0])
      index = found[1] && HardRPGKnownMethods.same?(found[1])
      memo = found[2] && HardRPGKnownMethods.same?(found[2])
      if index
        pos_nt = Game_CharacterBase.instance_method(:pos_nt?)
        pos = Game_CharacterBase.instance_method(:pos?)
        @pos_ok = Hash.new do |h, klass|
          # Owner and location, not ==: before Ruby 3.2 a method looked up from a subclass is never
          # == the same method looked up from its owner.
          same = ->(a, b) { a.owner.equal?(b.owner) && a.source_location == b.source_location }
          h[klass] = same.(klass.instance_method(:pos_nt?), pos_nt) && same.(klass.instance_method(:pos?), pos)
        end
      end
      Game_Map.prepend(Search)
      Game_Map.prepend(Index) if index
      Game_Map.prepend(Memo) if memo
      @status = "on (index #{index ? 'on' : 'off'}, memo #{memo ? 'on' : 'off'})"
    rescue StandardError => e
      @status = "off (#{e.class})"
    end
  end

  module Index
    def events_xy_nt(x, y)
      index = @hardrpg_jet_index
      return super unless index && Integer === x && Integer === y
      row = index[x]
      list = row && row[y]
      list ? list.dup : []
    end

    # {x => {y => [events]}} of the events that are not pass-through, in @events order; nil when
    # an event's class has its own pos? or a position is not an Integer.
    def hardrpg_jet_index
      ok = HardRPGJetPath.pos_ok
      index = {}
      @events.each_value do |event|
        return nil unless ok[event.class]
        next if event.instance_variable_get(:@through)
        x = event.instance_variable_get(:@x)
        y = event.instance_variable_get(:@y)
        return nil unless Integer === x && Integer === y
        ((index[x] ||= {})[y] ||= []) << event
      end
      index
    end
  end

  module Memo
    def passable?(x, y, d)
      memo = @hardrpg_jet_memo
      return super unless memo && Integer === x && Integer === y && Integer === d &&
                          x >= 0 && y >= 0 && y < 65536 && d >= 0 && d < 16
      key = (x << 20) | (y << 4) | d
      value = memo[key]
      value.nil? ? (memo[key] = super) : value
    end
  end

  module Search
    def find_path(tx, ty, sx, sy, dist, jump, char = $game_player)
      mode = HardRPGJetPath.mode
      return super unless mode & 1 != 0 && Integer === tx && Integer === ty && Integer === sx &&
                          Integer === sy && Integer === dist && sx >= 0 && sy >= 0 && sy < W
      saved = [@hardrpg_jet_index, @hardrpg_jet_memo]
      @hardrpg_jet_index = mode & 2 != 0 && respond_to?(:hardrpg_jet_index) ? hardrpg_jet_index : nil
      @hardrpg_jet_memo = mode & 4 != 0 ? {} : nil
      route = hardrpg_jet_search(tx, ty, sx, sy, dist, jump, char)
      route.equal?(:retry) ? super : route
    ensure
      @hardrpg_jet_index, @hardrpg_jet_memo = saved if saved
    end

    private

    def hardrpg_jet_search(tx, ty, sx, sy, dist, jump, char)
      return [] if (sx == tx && sy == ty) || (dist > 0 && (sx - tx).abs + (sy - ty).abs <= dist)
      return [] unless [2, 4, 6, 8].any? { |i| char.passable?(tx, ty, i) }
      max = Jet::Pathfinder::MAXIMUM_ITERATIONS
      start = sx * W + sy
      cost = { start => 0 } # every node seen, open or closed
      parent = { start => nil }
      # Open nodes by total cost; within a cost the first added comes first, as Array#min keeps
      # the first of equal nodes.
      buckets = [[start]]
      heads = [0]
      low = 0
      open = 1
      found = []
      iterations = 0
      loop do
        return [] if iterations == max
        iterations += 1
        return [] if open == 0
        low += 1 while (list = buckets[low]).nil? || heads[low] >= list.size
        current = list[heads[low]]
        heads[low] += 1
        open -= 1
        x = current / W
        y = current % W
        found.clear
        d = 2
        while d <= 8
          nx = round_x_with_direction(x, d)
          ny = round_y_with_direction(y, d)
          found << nx << ny if char.passable?(x, y, d)
          d += 2
        end
        step = cost[current] + 1
        i = 0
        while i < found.size
          nx = found[i]
          ny = found[i + 1]
          i += 2
          if (nx == tx && ny == ty) || (dist > 0 && (nx - tx).abs + (ny - ty).abs <= dist)
            return hardrpg_jet_route(nx, ny, current, parent, jump)
          end
          return :retry unless Integer === nx && Integer === ny && nx >= 0 && ny >= 0 && ny < W
          key = nx * W + ny
          next if cost.key?(key)
          cost[key] = step
          parent[key] = current
          total = step + (nx - tx).abs + (ny - ty).abs
          (buckets[total] ||= []) << key
          heads[total] ||= 0
          low = total if total < low
          open += 1
        end
      end
    end

    def hardrpg_jet_route(nx, ny, current, parent, jump)
      node = Node.new(Point.new(nx, ny))
      child = node
      key = current
      while key
        up = Node.new(Point.new(key / W, key % W))
        child.parent = up
        child = up
        key = parent[key]
      end
      node.mark_path
      recreate_path(node, jump)
    end
  end
end

# Khas Awesome Light Effects: shadows. Block_*#render_shadow clears one strip of a light for every
# pixel column (or row) a wall's shadow covers, a Ruby loop of several hundred clear_rect calls for
# each light with shadows: every frame for a moving light (a lantern), and for each static light on
# a map load. The same slopes are set here, then the engine runs the loop with the same Float
# arithmetic and clears the same strips in one draw. Each class is switched only while its methods
# are the script's text; anything unusual runs the script's own loop.
module HardRPGKhasShadows
  KNOWN = {
    'Block_SL' => %w[1478767641:3248573000:234 1868431261:3372878644:33 455519482:3369077555:33],
    'Block_SR' => %w[1871759265:1251298339:239 1868431261:3372878644:33 455519482:3369077555:33],
    'Block_IL' => %w[1720142286:2120241444:265 1868431261:3372878644:33 455519482:3369077555:33],
    'Block_IR' => %w[3972344169:703484669:270 1868431261:3372878644:33 455519482:3369077555:33],
    'Block_WL' => %w[2474174396:3248179783:234 1868431261:3372878644:33 455519482:3369077555:33],
    'Block_WR' => %w[2764837892:1250905122:239 1868431261:3372878644:33 455519482:3369077555:33],
    'Block_SU' => %w[4075887539:2732614210:325 3597526642:3469938685:52 4065797967:3465023486:52],
    'Block_SD' => %w[742787775:3108987832:326 3597526642:3469938685:52 4065797967:3465023486:52],
  }.freeze
  HELPERS = { 'Block_SU' => %w[shadow_ix shadow_fx], 'Block_SD' => %w[shadow_ix shadow_fx] }.freeze

  # Left-facing walls (columns from the wall to the light's far edge), right-facing walls.
  module SL
    def render_shadow(phx, phy, sx, sy, range, bitmap)
      @m1 = (@y1-sy)/(@x1-sx)
      @n1 = sy - @m1*sx
      @m2 = (@y2-sy)/(@x2-sx)
      @n2 = sy - @m2*sx
      super unless bitmap.__hardrpg_khas_strips(0, @x1, sx+range, @m1, @n1, @m2, @n2, phx, phy,
                                                 HardRPGKhasShadows.pad(self), 0, 0)
    end
  end
  module SR
    def render_shadow(phx, phy, sx, sy, range, bitmap)
      @m1 = (@y1-sy)/(@x1-sx)
      @n1 = sy - @m1*sx
      @m2 = (@y2-sy)/(@x2-sx)
      @n2 = sy - @m2*sx
      super unless bitmap.__hardrpg_khas_strips(0, (sx-range).to_i, @x1, @m1, @n1, @m2, @n2, phx, phy,
                                                 HardRPGKhasShadows.pad(self), 0, 0)
    end
  end
  module IL
    def render_shadow(phx, phy, sx, sy, range, bitmap)
      @m1 = (@y1-sy)/(@x1-sx)
      @n1 = @y1 - @m1*@x1
      @m2 = (@y2-sy)/(@x2-sx)
      @m2 = 0 if @m2 > 0
      @n2 = @y2 - @m2*@x2
      super unless bitmap.__hardrpg_khas_strips(1, @x1, sx+range, @m1, @n1, @m2, @n2, phx, phy, 3, 0, 0)
    end
  end
  module IR
    def render_shadow(phx, phy, sx, sy, range, bitmap)
      @m1 = (@y1-sy)/(@x1-sx)
      @n1 = @y1 - @m1*@x1
      @m2 = (@y2-sy)/(@x2-sx)
      @m2 = 0 if @m2 < 0
      @n2 = @y2 - @m2*@x2
      super unless bitmap.__hardrpg_khas_strips(1, (sx-range).to_i, @x1, @m1, @n1, @m2, @n2, phx, phy, 3, 0, 0)
    end
  end
  module SU
    def render_shadow(phx, phy, sx, sy, range, bitmap)
      acc = HardRPGKhasShadows.acc(self)
      if @x1 == sx
        @m1 = nil
      else
        @m1 = (@y1-sy)/(@x1-sx)
        @m1 += acc if @m1 < -acc
        @n1 = @y1 - @m1*@x1
      end
      if @x2 == sx
        @m2 = nil
      else
        @m2 = (@y2-sy)/(@x2-sx)
        @n2 = sy - @m2*sx
      end
      super unless bitmap.__hardrpg_khas_strips(2, @y1, sy+range, @m1, @n1, @m2, @n2, phx, phy, 0, @x1, @x2)
    end
  end
  module SD
    def render_shadow(phx, phy, sx, sy, range, bitmap)
      acc = HardRPGKhasShadows.acc(self)
      if @x1 == sx
        @m1 = nil
      else
        @m1 = (@y1-sy)/(@x1-sx)
        @m1 -= acc if @m1 > acc
        @n1 = sy - @m1*sx
      end
      if x2 == sx
        @m2 = nil
      else
        @m2 = (@y2-sy)/(@x2-sx)
        @n2 = sy - @m2*sx
      end
      super unless bitmap.__hardrpg_khas_strips(2, (sy-range).to_i, @y1, @m1, @n1, @m2, @n2, phx, phy, 0, @x1, @x2)
    end
  end
  MODULES = { 'Block_SL' => SL, 'Block_WL' => SL, 'Block_SR' => SR, 'Block_WR' => SR,
              'Block_IL' => IL, 'Block_IR' => IR, 'Block_SU' => SU, 'Block_SD' => SD }.freeze
  PAD = { 'Block_SL' => 3, 'Block_SR' => 3, 'Block_WL' => 2, 'Block_WR' => 2 }.freeze

  @status = nil
  @pads = {}
  @accs = {}
  class << self
    attr_reader :status

    def pad(surface)
      @pads[surface.class]
    end

    def acc(surface)
      @accs[surface.class]
    end

    def install
      return @status if @status
      return @status = 'off (disabled)' if $hardrpg_vxace_map_speedups == false || $hardrpg_khas_shadows == false
      return @status = 'off (no script)' unless defined?(Block_Surface) && defined?(HardRPGNative::Norm) &&
                                                Bitmap.method_defined?(:__hardrpg_khas_strips)
      found = HardRPGVXAceScanCache.fetch(:khas_shadows) do
        KNOWN.each_with_object({}) do |(cname, (render, iy, fy)), out|
          names = HELPERS[cname] || %w[shadow_iy shadow_fy]
          locs = HardRPGKnownMethods.locate("#{cname}#render_shadow" => render, "#{cname}##{names[0]}" => iy,
                                            "#{cname}##{names[1]}" => fy)
          out[cname] = locs if locs
        end
      end
      on = []
      found.each do |cname, locs|
        next unless HardRPGKnownMethods.same?(locs)
        klass = Object.const_get(cname)
        @pads[klass] = PAD[cname]
        @accs[klass] = klass.const_get(:ACC) if %w[Block_SU Block_SD].include?(cname)
        klass.prepend(MODULES[cname])
        on << cname.sub('Block_', '')
      end
      @status = on.empty? ? 'off (other script)' : "on (#{on.join(' ')})"
    rescue StandardError => e
      @status = "off (#{e.class})"
    end
  end
end

# rgss_main runs once every script section is defined, and loads the database first.
# define_method, as in crash_rescue.rb: the engine runs postload scripts from inside rgss_main, and
# a `def` inside `module Kernel` would give them Kernel as their namespace.
if Kernel.respond_to?(:rgss_main, true)
  Kernel.send(:alias_method, :hardrpg_database_load_rgss_main, :rgss_main)
  Kernel.send(:private, :hardrpg_database_load_rgss_main)
  Kernel.send(:define_method, :rgss_main) do |*args, &block|
    HardRPGDatabaseLoad.wrap
    hardrpg_database_load_rgss_main(*args, &block)
  end
  Kernel.send(:private, :rgss_main)
end

# Installed at the first map frame: checking the game's scripts takes up to a second on the Vita for a
# big game, which would hold up its title screen; a map load already waits for files.
class << Graphics
  alias_method :hardrpg_vxace_map_speedups_update, :update
  def update(*args)
    if !HardRPGRefreshFilter.status && defined?(SceneManager) && defined?(Scene_Map) &&
       (SceneManager.scene.is_a?(Scene_Map) rescue false)
      HardRPGRefreshFilter.install
      HardRPGAnimeChipSpeedup.install
      HardRPGSwitchSkip.install
      HardRPGOffscreenSprites.install
      HardRPGJetPath.install
      HardRPGKhasShadows.install
      HardRPGSpriteScripts.install
      HardRPGSteadySprites.install
      HardRPGVXAceScanCache.save
    end
    hardrpg_vxace_map_speedups_update(*args)
  end
end
