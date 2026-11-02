# VX Ace map speedups (preload). Installed at the game's first frame, once every script section has
# run, and only where the game's code is the code each speedup was written for.
#
# Set $hardrpg_vxace_map_speedups = false in a per-game shim to keep every method as the game has it.

# What the script checks found, kept per game (by the scripts' checksum): checking a big game's
# scripts takes 1-3 s on the Vita, and its scripts do not change between plays. Only what the
# scripts say is kept; whether methods are still native is checked on every start.
require 'zlib' unless defined?(Zlib)

module HardRPGVXAceScanCache
  VERSION = 1
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
  SAFE = %w[set_character_bitmap update_src_rect update_position move_animation update_bitmap
            graphic_changed? set_tile_bitmap tileset_bitmap note].freeze

  @status = nil
  class << self
    attr_reader :status

    def check(defs, swapped)
      %w[Spriteset_Map#update_characters Sprite_Character#update].each do |key|
        return "off (#{key} not native)" unless swapped.include?(key)
      end
      check_scripts(defs)
    end

    def check_scripts(defs)
      defs.each do |key, list|
        cname, name = key.split('#', 2)
        next unless cname == 'Sprite_Character' && !SAFE.include?(name)
        return "off (custom Sprite_Character##{name})" if list.size > 1
      end
      'on'
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
        check_scripts(HardRPGRefreshFilter.definitions($RGSS_SCRIPTS || [], %w[Sprite_Character]))
      end
      HardRPGNative.set_option('offscreen_skip', true) if @status == 'on'
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
      HardRPGVXAceScanCache.save
    end
    hardrpg_vxace_map_speedups_update(*args)
  end
end
