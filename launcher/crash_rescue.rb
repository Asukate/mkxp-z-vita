# HardRPG crash rescue.
#
# Keeps a copy of the game's own save data from the last map change or
# battle start. The engine writes each copy to ux0:/data/hardrpg/rescue/<game>.*
# on a background thread, so a crash, a power loss or a closed app leaves
# the latest one behind, and the launcher offers to resume from it. The
# game's own save files are never written or renamed.
#
# The copy is made by the game's own save code: DataManager.make_save_header
# and make_save_contents on VX Ace, write_save_data on XP and VX. Scripts
# that add data to saves through those methods are therefore included.
#
# Copies are taken only where crashes tend to happen and the screen is
# already still or dark, so the cost does not show as a stutter:
#   transfer  the new map is set up while the screen is dark or frozen;
#   battle    the battle is about to start.
# Opening the menu takes none: that is where the player saves anyway.
# Copies are spaced by their own cost: one that took T ms waits T/20 seconds
# (between 10 and 120 s) before the next.
#
# A copy is only taken when saving there would lose nothing: no message on
# screen, and the map's event either not running or stopped on a command
# that a save resumes correctly (Transfer Player, or Battle Processing, which
# is rewound so a resumed game starts that battle again).
#
# Faster copies: self switches (often the largest part of a save) are written
# from the bytes of the previous copy while they have not changed. Every
# method of Game_SelfSwitches except the reader marks them changed (games add
# their own, e.g. delete or restore-all), as does a different table or a
# different size; they are written in full at least every REUSE_MAX copies.
# The game's own saves are not affected: the reuse is switched on only while
# a copy is made.
#
# Static data (the map with its events, pages, command lists and move routes)
# is named instead of written when it is proven unchanged: its fingerprint (a
# hash of the whole content, made by the engine) matches the one taken when
# the game loaded the file. The resume loads the file again and checks the fingerprint
# before using it, so data the game changed while running is always written
# in full, and a rescue never resumes with different data. See Static.
#
# When the game saves through its own code, the rescue is dropped (the
# player's save is newer). The engine drops it too when the game ends
# normally. The setting (launcher, per game, or the in-game menu) is read
# before every copy; switching it off drops the rescue.
#
# The engine sets $hardrpg_rescue_base (the file name without extension) and
# $hardrpg_rescue_rgss before preload scripts run; without them this file
# does nothing.
module HardRPGRescue
  BASE = $hardrpg_rescue_base
  RGSS = $hardrpg_rescue_rgss.to_i
  FORMAT = 2
  GAP_MIN = 10
  GAP_MAX = 120
  REUSE_MAX = 6
  COMPRESS = 1 # zlib level for the copy; 0 keeps it as is
  STATS = Hash.new(0)
  FILES = %w[.rescue .meta .json .shot].freeze

  # Marshal.dump writes to anything with #write.
  class Sink
    attr_reader :data
    def initialize; @data = String.new.force_encoding(Encoding::ASCII_8BIT); end
    def write(s); @data << s; s.bytesize; end
  end

  # A copy names the game's static data with these: kind, file, fingerprint
  # and position, one per line. Loading a copy turns each back into the data
  # (Static.resolve).
  class Ref
    def initialize(text)
      @text = text
    end

    def _dump(_level)
      @text
    end

    def self._load(text)
      HardRPGRescue::Static.resolve(text)
    end
  end

  # Static data by reference. When the game loads a map, its fingerprint is
  # kept (the first load of each map file in the session). At a copy, the
  # live map is fingerprinted again; when it still matches, the map and
  # everything in it (events, pages, command lists, move routes) are written
  # as Refs, where the stock scripts hold them (HELD). Common events are not: fingerprinting them costs 70-200 ms on the
  # Vita for BS1/BS2, more than naming them saves. Only the holders' instance
  # variables are swapped for Refs, while Marshal writes, and put back after.
  # The lookup from an object to its place in the map is built the first time
  # a copy is made on that map, and kept while the map stays loaded.
  module Static
    MAP_FILE = /map0*(\d+)\.r[xv]data2?\z/i
    # Where the stock scripts keep map data outside the map itself (Game_Map,
    # Game_Event, Game_Character, Game_Interpreter). Data a game keeps
    # elsewhere is written in full.
    HELD = %i[@map @event @page @list @move_route @original_move_route].freeze
    @maps = {}         # map id => [file, fingerprint] as loaded
    @index = nil       # the current map's objects => [kind, event id, page]
    @index_map = nil
    @resolving = nil
    class << self
      attr_reader :maps
      attr_accessor :off # tests: copies write everything

      def engine
        return HardRPGRescueNative if defined?(HardRPGRescueNative) && HardRPGRescueNative.respond_to?(:fingerprint)
        return RescueFP if defined?(RescueFP) # desktop tests
        nil
      end

      def fp(object)
        engine.fingerprint(object)
      end

      # Every load_data passes through here once the hook is installed.
      def loaded(name, data)
        return unless engine && name.is_a?(String)
        if (m = MAP_FILE.match(name))
          id = m[1].to_i
          return if @maps.key?(id) || !data.respond_to?(:events)
          t = HardRPGRescue.clock
          @maps[id] = [name, fp(data)]
          HardRPGRescue::STATS['map fingerprint ms'] += ((HardRPGRescue.clock - t) * 1000).round(1)
        end
      rescue StandardError => e
        HardRPGRescue.log("fingerprint failed (#{e.class}: #{e.message[0, 120]})")
      end


      # The hook goes on whichever load_data the game calls (a game may define
      # its own at the top level), after the game's scripts are defined, so
      # what it sees is what the game's own loading gives.
      def hook
        return if @hooked || !engine
        @hooked = true
        owner = Object.instance_method(:load_data).owner rescue Kernel
        owner.send(:alias_method, :hardrpg_rescue_load_data, :load_data)
        owner.send(:private, :hardrpg_rescue_load_data)
        owner.send(:define_method, :load_data) do |*args, &block|
          data = hardrpg_rescue_load_data(*args, &block)
          HardRPGRescue::Static.loaded(args[0], data)
          data
        end
        owner.send(:private, :load_data)
      end

      def index_for(map)
        return @index if @index_map.equal?(map)
        index = {}.compare_by_identity
        index[map] = ['M']
        map.events.each do |eid, event|
          index[event] ||= ['E', eid]
          event.pages.each_with_index do |page, pi|
            index[page] ||= ['P', eid, pi]
            index[page.list] ||= ['L', eid, pi]
            index[page.move_route] ||= ['R', eid, pi]
          end
        end
        @index_map = map
        @index = index
      end


      # Objects whose instance variables may hold static data.
      def holders
        list = [$game_map]
        events = $game_map.instance_variable_get(:@events)
        list.concat(events.values) if events.is_a?(Hash)
        list << $game_player if $game_player
        work = [$game_map.instance_variable_get(:@interpreter)]
        work << $game_system.instance_variable_get(:@map_interpreter) if $game_system
        list.each { |h| work << h.instance_variable_get(:@interpreter) }
        common = $game_map.instance_variable_get(:@common_events)
        common.each { |c| work << c.instance_variable_get(:@interpreter) } if common.respond_to?(:each)
        common.each_value { |c| work << c.instance_variable_get(:@interpreter) } if common.is_a?(Hash)
        work.compact!
        seen = {}.compare_by_identity
        while (i = work.shift)
          next if seen[i]
          seen[i] = true
          list << i
          child = i.instance_variable_get(:@child_interpreter)
          work << child if child
        end
        list.compact
      end

      # The Refs this copy may use, or nil.
      def plan
        return nil if @off || !engine || !$game_map
        map = $game_map.instance_variable_get(:@map)
        id = $game_map.map_id
        plan = { refs: 0 }
        t = HardRPGRescue.clock
        entry = @maps[id]
        if map && entry && (live = fp(map)) == entry[1]
          plan[:map] = [index_for(map), "#{entry[0]}\n#{live}"]
        elsif map && entry
          HardRPGRescue::STATS['map changed since loaded'] += 1
        end
        plan[:fp_ms] = ((HardRPGRescue.clock - t) * 1000).round(1)
        plan
      end

      def ref_for(value, plan)
        if plan[:map] && (where = plan[:map][0][value])
          kind, eid, page = where
          return Ref.new([kind, plan[:map][1], eid, page].compact.join("\n"))
        end
        nil
      end

      # Runs the block (which writes the copy) with Refs swapped in, and once
      # more without them if that fails. Returns [result, refs used].
      def around(meta)
        plan = (plan() rescue nil)
        return [yield, 0] unless plan && plan[:map]
        changed = []
        t = HardRPGRescue.clock
        begin
          holders.each do |h|
            HELD.each do |name|
              value = h.instance_variable_get(name)
              next if value.nil?
              ref = ref_for(value, plan)
              next unless ref
              changed << [h, name, value]
              h.instance_variable_set(name, ref)
            end
          end
          meta[:swap_ms] = ((HardRPGRescue.clock - t) * 1000).round(1)
          result = yield
          meta[:fp_ms] = plan[:fp_ms]
          [result, changed.size]
        rescue StandardError => e
          raise if changed.empty?
          restore(changed)
          changed = []
          HardRPGRescue.log("copy with references failed (#{e.class}: #{e.message[0, 120]}); written in full")
          HardRPGRescue::STATS['references failed'] += 1
          [yield, 0]
        ensure
          restore(changed)
        end
      end

      def restore(changed)
        changed.reverse_each { |h, name, value| h.instance_variable_set(name, value) }
        changed.clear
      end

      # --- resume ---
      def resolving
        @resolving = {}
        @checked = nil
        yield
      ensure
        @resolving = nil
      end

      def resolve(text)
        kind, file, want, a, b = text.split("\n")
        want = want.to_i
        map = (@resolving ||= {})[file] ||= load_data(file)
        raise "#{file} is not the map the rescue named" unless checked(map, want)
        case kind
        when 'M' then map
        when 'E' then map.events.fetch(a.to_i)
        when 'P' then map.events.fetch(a.to_i).pages.fetch(b.to_i)
        when 'L' then map.events.fetch(a.to_i).pages.fetch(b.to_i).list
        when 'R' then map.events.fetch(a.to_i).pages.fetch(b.to_i).move_route
        else raise "unknown reference #{kind}"
        end
      end

      def checked(object, want)
        @checked ||= {}.compare_by_identity
        @checked[object] = (fp(object) == want) unless @checked.key?(object)
        @checked[object]
      end
    end
  end

  # The self-switch bytes of the previous copy, and whether they still hold.
  module SelfSwitches
    @dirty = true
    @blob = nil
    @owner = nil
    @table = nil
    @size = -1
    @uses = 0
    class << self
      attr_accessor :dirty
      def bytes(owner)
        table = owner.instance_variable_get(:@data)
        if @dirty || !@owner.equal?(owner) || !@table.equal?(table) || !table.respond_to?(:size) ||
           table.size != @size || @uses >= REUSE_MAX
          state = {}
          owner.instance_variables.each { |name| state[name] = owner.instance_variable_get(name) }
          @blob = Marshal.dump(state)
          @owner = owner
          @table = table
          @size = table.respond_to?(:size) ? table.size : -1
          @dirty = false
          @uses = 0
          HardRPGRescue::STATS['self switches written'] += 1
        else
          @uses += 1
          HardRPGRescue::STATS['self switches reused'] += 1
        end
        @blob
      end
    end
  end

  @pending = false
  @last_at = nil
  @last_cost = 0.0
  @failures = 0
  @busy = false
  @started = nil
  @transferring = false
  @was_on = true

  class << self
    attr_reader :pending, :last_at
    attr_accessor :transferring

    def ready?
      BASE && RGSS.between?(1, 3) && @failures < 3
    end

    def native?
      defined?(HardRPGRescueNative)
    end

    # The setting as the engine has it now (the in-game menu can change it).
    def enabled?
      return true unless native?
      HardRPGRescueNative.enabled?
    rescue StandardError
      true
    end

    def clock
      Process.clock_gettime(Process::CLOCK_MONOTONIC)
    rescue StandardError
      Time.now.to_f
    end

    def log(text)
      $stderr.puts("Crash rescue: #{text}")
    rescue StandardError
      nil
    end

    # Called once at boot. A resume request from the launcher is used once:
    # the request file is removed before anything else happens.
    def boot
      return unless ready?
      @started = clock
      request = BASE + '.resume'
      return unless File.exist?(request)
      File.delete(request) rescue nil
      if File.exist?(BASE + '.rescue') && File.exist?(BASE + '.meta')
        @pending = true
        log('resume requested')
      else
        log('resume requested but no rescue files')
      end
    end

    def interpreter
      RGSS == 1 ? $game_system.map_interpreter : $game_map.interpreter
    end

    # The command the map's event interpreter is stopped on, or nil when it
    # is not running.
    def command_code(i)
      return nil unless i && i.running?
      list = i.instance_variable_get(:@list)
      index = i.instance_variable_get(:@index).to_i
      RGSS == 3 ? (list && list[index] && list[index].code) : (list && index > 0 && list[index - 1] && list[index - 1].code)
    end

    def message_up?
      case RGSS
      when 3 then $game_message.busy?
      when 2 then $game_message.visible
      else $game_temp && $game_temp.message_window_showing
      end
    end

    # nil when a copy here would be faithful, else a reason it would not.
    def unsafe(allowed)
      return 'message' if message_up?
      i = interpreter
      # XP and VX keep an event's whole state in plain variables (no fiber),
      # so a copy mid-event resumes it; only a message it waits on is lost.
      return (i && i.instance_variable_get(:@message_waiting) ? 'message' : nil) if RGSS < 3
      code = command_code(i)
      return nil if code.nil? || allowed.include?(code)
      "event #{code}"
    end

    # The rescue no longer applies: the game saved, or the setting is off.
    def drop(why)
      $hardrpg_rescue_data = nil
      $hardrpg_rescue_meta = nil
      $hardrpg_rescue_json = nil
      @meta = nil
      SelfSwitches.dirty = true
      if native?
        HardRPGRescueNative.clear
      else
        FILES.each { |ext| File.delete(BASE + ext) if File.exist?(BASE + ext) }
      end
      STATS["dropped (#{why})"] += 1
      log("dropped (#{why})")
    rescue StandardError => e
      log("drop failed (#{e.class}: #{e.message[0, 120]})")
    end

    def take(reason, allowed = [], rewind = false)
      return unless ready? && !@busy
      unless enabled?
        drop('setting off') if @was_on
        @was_on = false
        return
      end
      @was_on = true
      return unless due?
      if (why = unsafe(allowed + (rewind ? [301] : [])))
        STATS["skip #{reason} (#{why})"] += 1
        return
      end
      rewind = rewind && command_code(interpreter) == 301
      @busy = true
      @copy_meta = {}
      @refs = 0
      t = clock
      raw = with_reuse { RGSS == 3 ? dump_vxace(rewind) : dump_legacy(rewind) }
      raw.force_encoding(Encoding::ASCII_8BIT)
      dumped = clock
      data = COMPRESS > 0 && defined?(Zlib) ? Zlib::Deflate.deflate(raw, COMPRESS) : raw
      meta = describe(reason, raw.bytesize, data.bytesize, data.equal?(raw) ? 0 : COMPRESS)
      meta['dump_ms'] = ((dumped - t) * 1000).round(1)
      meta['total_ms'] = ((clock - t) * 1000).round(1)
      meta['refs'] = @refs.to_i
      meta['fp_ms'] = @copy_meta[:fp_ms] if @copy_meta[:fp_ms]
      meta['swap_ms'] = @copy_meta[:swap_ms] if @copy_meta[:swap_ms]
      meta['written_at'] = Time.now.to_i
      $hardrpg_rescue_data = data
      $hardrpg_rescue_meta = Marshal.dump(meta)
      $hardrpg_rescue_json = json(meta)
      @meta = meta
      @last_at = clock
      @last_cost = meta['total_ms']
      HardRPGRescueNative.flush(data, $hardrpg_rescue_meta, $hardrpg_rescue_json) if native?
      STATS["took #{reason}"] += 1
      log("#{reason} map=#{meta['map_id']} bytes=#{raw.bytesize}->#{data.bytesize} dump=#{meta['dump_ms']}ms total=#{meta['total_ms']}ms refs=#{meta['refs']}#{rewind ? ' (battle rewound)' : ''}")
    rescue Exception => e
      raise if e.is_a?(SystemExit) || (defined?(RGSSReset) && e.is_a?(RGSSReset))
      @failures += 1
      STATS["copy failed (#{e.class})"] += 1
      log("copy failed (#{e.class}: #{e.message[0, 160]})")
    ensure
      @busy = false
    end

    def due?
      return true if @last_at.nil?
      gap = [[@last_cost / 20.0, GAP_MIN].max, GAP_MAX].min
      clock - @last_at >= gap
    end

    def map_id
      $game_map ? $game_map.map_id : 0
    end

    # Self switches come from SelfSwitches.bytes while a copy is made. A game
    # that writes them its own way (its own _dump or marshal_dump) keeps it.
    def with_reuse
      klass = defined?(Game_SelfSwitches) ? Game_SelfSwitches : nil
      own = klass && (klass.method_defined?(:_dump) || klass.method_defined?(:marshal_dump) ||
                      klass.private_method_defined?(:marshal_dump))
      klass.send(:define_method, :_dump) { |_level| HardRPGRescue::SelfSwitches.bytes(self) } if klass && !own
      yield
    ensure
      klass.send(:remove_method, :_dump) if klass && !own && klass.instance_methods(false).include?(:_dump)
    end

    # The game's own VX Ace save code, minus the file: on_before_save, then the
    # header and contents. The save counter is put back, so the game cannot
    # tell a copy was made.
    def dump_vxace(rewind)
      system = $game_system
      count = system.instance_variable_get(:@save_count)
      events = $game_map.interpreter
      index = events.instance_variable_get(:@index)
      system.on_before_save
      # Battle music is already playing at the battle point; resume plays the
      # map's own music, as BattleManager saved it.
      if SceneManager.scene_is?(Scene_Battle) && BattleManager.instance_variable_get(:@map_bgm)
        system.instance_variable_set(:@bgm_on_save, BattleManager.instance_variable_get(:@map_bgm))
        system.instance_variable_set(:@bgs_on_save, BattleManager.instance_variable_get(:@map_bgs))
      end
      # Game_Interpreter#marshal_dump stores @index + 1 (the command after the
      # one that waits): one less makes a resumed game run Battle Processing.
      events.instance_variable_set(:@index, index - 1) if rewind
      header = DataManager.make_save_header
      contents = DataManager.make_save_contents
      data, @refs = Static.around(@copy_meta) { Marshal.dump(header) << Marshal.dump(contents) }
      @save_count = count
      data
    ensure
      events.instance_variable_set(:@index, index) if rewind && events
      system.instance_variable_set(:@save_count, count) if system
    end

    # XP (Scene_Save) and VX (Scene_File) write_save_data, into memory.
    def dump_legacy(rewind)
      system = $game_system
      count = system.save_count
      events = interpreter
      index = events.instance_variable_get(:@index)
      # XP and VX advance past Battle Processing before the battle starts.
      events.instance_variable_set(:@index, index - 1) if rewind
      # XP plays the new map's music after the fade-in: the copy, made during
      # the transfer, records it as Game_Map#autoplay will.
      music = nil
      if RGSS == 1 && @transfer_music
        music = [system.instance_variable_get(:@playing_bgm), system.instance_variable_get(:@playing_bgs)]
        # XP's Game_Map has no music readers; the map data has them.
        data = $game_map.instance_variable_get(:@map)
        system.instance_variable_set(:@playing_bgm, data.bgm) if data && data.autoplay_bgm
        system.instance_variable_set(:@playing_bgs, data.bgs) if data && data.autoplay_bgs
      end
      scene = RGSS == 1 ? Scene_Save.allocate : Scene_File.allocate
      data, @refs = Static.around(@copy_meta) do
        system.save_count = count
        sink = Sink.new
        scene.send(:write_save_data, sink)
        sink.data
      end
      @save_count = count
      data
    ensure
      events.instance_variable_set(:@index, index) if rewind && events
      if music
        system.instance_variable_set(:@playing_bgm, music[0])
        system.instance_variable_set(:@playing_bgs, music[1])
      end
      system.save_count = count if count
    end

    def describe(reason, raw, kept, level)
      leader = (RGSS == 1 ? $game_party.actors : $game_party.members).first rescue nil
      name = ($game_map.display_name.to_s rescue '') if RGSS == 3
      name = (load_data(RGSS == 1 ? 'Data/MapInfos.rxdata' : 'Data/MapInfos.rvdata')[map_id].name.to_s rescue '') if RGSS < 3
      {
        'format' => FORMAT, 'rgss' => RGSS, 'reason' => reason,
        'map_id' => map_id, 'map_name' => name.to_s,
        'x' => ($game_player.x rescue 0), 'y' => ($game_player.y rescue 0),
        'leader' => (leader ? leader.name.to_s : ''), 'level' => (leader ? leader.level : 0),
        'playtime_s' => (Graphics.frame_count / Graphics.frame_rate rescue 0),
        'save_count' => @save_count.to_i,
        'last_index' => (RGSS == 3 ? DataManager.last_savefile_index : 0) || 0,
        'save_disabled' => ($game_system.save_disabled ? true : false),
        'bytes' => raw, 'kept_bytes' => kept, 'compressed' => level,
        'session_s' => (clock - (@started || clock)).round(1)
      }
    end

    def json(value)
      case value
      when Hash then '{' + value.map { |k, v| json(k.to_s) + ':' + json(v) }.join(',') + '}'
      when String
        s = value.dup.force_encoding(Encoding::UTF_8)
        s = s.encode(Encoding::UTF_8, invalid: :replace, undef: :replace, replace: '?') unless s.valid_encoding?
        '"' + s.gsub(/["\\\x00-\x1f]/) { |c| format('\\u%04x', c.ord) } + '"'
      when Float then value.finite? ? value.to_s : '0'
      when nil then 'null'
      else value.to_s
      end
    end

    # The engine calls this when a Ruby error ends the game, just before it
    # writes the files: the launcher's description of the copy.
    def report(error)
      return nil unless @meta
      meta = @meta.dup
      meta['error'] = error.to_s
      meta['age_s'] = (clock - @last_at).round(1)
      meta['written_at'] = Time.now.to_i
      meta['stats'] = STATS.map { |k, v| "#{k}: #{v}" }.join(', ')
      $hardrpg_rescue_json = json(meta)
      place = meta['map_name'].to_s.empty? ? "map #{meta['map_id']}" : "map #{meta['map_id']} (#{meta['map_name']})"
      $hardrpg_rescue_summary = "the game as it was #{meta['age_s'].round} s before the error " \
                                "(#{meta['reason']}, #{place}) was kept. Start the game again from the launcher to resume there."
    end

    # --- resuming -----------------------------------------------------------

    def read_rescue
      meta = Marshal.load(File.binread(BASE + '.meta'))
      raise 'rescue format' unless [1, FORMAT].include?(meta['format']) && meta['rgss'] == RGSS
      data = File.binread(BASE + '.rescue')
      raw = meta['compressed'].to_i > 0 ? Zlib::Inflate.inflate(data) : data
      raise 'rescue size' unless raw.bytesize == meta['bytes']
      # The game's own loader opens a file by name: give it a plain copy.
      path = BASE + '.load'
      File.open(path, 'wb') { |f| f.write(raw) }
      [meta, data, path]
    end

    # After a resume the copy that was loaded is the one to keep, so an error
    # before the next safe moment writes the same rescue again. The launcher
    # stops offering it until then.
    def resumed(meta, data, path)
      File.delete(path) rescue nil
      File.delete(BASE + '.json') rescue nil
      $hardrpg_rescue_data = data
      $hardrpg_rescue_meta = Marshal.dump(meta)
      @meta = meta
      @last_at = clock
      SelfSwitches.dirty = true
      log("resumed at map #{meta['map_id']} (#{meta['reason']})")
    end

    def resume_failed(e, path)
      File.delete(path) rescue nil if path
      log("resume failed (#{e.class}: #{e.message[0, 160]})")
      File.open(BASE + '.failed', 'w') { |f| f.write("#{e.class}: #{e.message[0, 400]}\n") } rescue nil
    end

    # VX Ace: the game's load_game_without_rescue reads the copy, so scripts
    # that extend loading take part. make_filename points at it meanwhile.
    def resume_vxace
      @pending = false
      meta, data, path = read_rescue
      manager = DataManager.singleton_class
      manager.send(:alias_method, :hardrpg_rescue_filename, :make_filename)
      manager.send(:define_method, :make_filename) { |_index| path }
      begin
        ok = Static.resolving { DataManager.load_game_without_rescue(meta['last_index'].to_i) }
        raise 'the game could not load the rescue' unless ok
      ensure
        manager.send(:alias_method, :make_filename, :hardrpg_rescue_filename)
      end
      $game_system.instance_variable_set(:@save_count, meta['save_count'])
      $game_system.on_after_load
      settle_input
      resumed(meta, data, path)
      true
    rescue Exception => e
      raise if e.is_a?(SystemExit)
      resume_failed(e, path)
      DataManager.create_game_objects rescue nil # the title starts from clean objects
      false
    end

    # A resume skips the title, so the resumed map gets the session's first
    # Input.update. Some games press a key there: To the Moon's mouse script
    # reports a right click (B, the menu) on its first update. Run the game's
    # Input.update twice here so that press is used up before the map runs.
    def settle_input
      2.times { Input.update }
    rescue StandardError => e
      log("input settle failed (#{e.class})")
    end

    # XP and VX: what Continue does (fresh temporary state, the save's objects,
    # its music, the map), without the load screen's windows. Some games have
    # no title scene (To the Moon draws its title on the start map), so this
    # runs on the first frame where the game's data is loaded, whatever the
    # scene is; see tick for the scenes that may still follow.
    def resume_legacy
      @pending = false
      meta, data, path = read_rescue
      $game_temp = Game_Temp.new
      if RGSS == 1
        Static.resolving { File.open(path, 'rb') { |f| Scene_Load.allocate.send(:read_save_data, f) } }
        $game_system.bgm_play($game_system.playing_bgm)
        $game_system.bgs_play($game_system.playing_bgs)
        $game_map.update
      else
        scene = Scene_File.allocate
        Static.resolving { File.open(path, 'rb') { |f| scene.send(:read_save_data, f) } }
        [scene.instance_variable_get(:@last_bgm), scene.instance_variable_get(:@last_bgs)].each { |sound| sound.play if sound }
      end
      $game_system.save_count = meta['save_count']
      @resume_scene = $scene = Scene_Map.new
      @resume_guard = 600
      settle_input
      resumed(meta, data, path)
    rescue Exception => e
      raise if e.is_a?(SystemExit)
      @resume_scene = nil
      resume_failed(e, path)
    end

    # --- hooks --------------------------------------------------------------

    # Every method that could change self switches marks them changed; only
    # the reader is left alone. Methods defined later are wrapped as they come.
    def watch_self_switches
      return unless defined?(Game_SelfSwitches)
      klass = Game_SelfSwitches
      wrap = lambda do |name|
        next if name == :[] || name == :_dump || name.to_s.start_with?('hardrpg_rescue_')
        plain = :"hardrpg_rescue_#{name.to_s.gsub(/[^A-Za-z0-9_]/) { |c| format('x%02x', c.ord) }}"
        next if klass.method_defined?(plain) || klass.private_method_defined?(plain)
        hidden = klass.private_method_defined?(name)
        klass.send(:alias_method, plain, name)
        klass.send(:define_method, name) do |*args, &block|
          HardRPGRescue::SelfSwitches.dirty = true
          send(plain, *args, &block)
        end
        klass.send(:private, name) if hidden
      end
      (klass.instance_methods(false) + klass.private_instance_methods(false)).each { |name| wrap.call(name) }
      watcher = wrap
      klass.define_singleton_method(:method_added) do |name|
        super(name) rescue nil
        next if @hardrpg_rescue_wrapping
        @hardrpg_rescue_wrapping = true
        begin
          watcher.call(name)
        ensure
          @hardrpg_rescue_wrapping = false
        end
      end
      # Copies hold self switches written by SelfSwitches.bytes.
      klass.define_singleton_method(:_load) do |bytes|
        object = allocate
        Marshal.load(bytes).each { |name, value| object.instance_variable_set(name, value) }
        object
      end
    end

    # The game saved: its save is newer than any rescue.
    def saved
      return if @busy
      drop('the game saved')
    end

    # VX Ace: rgss_main runs after every script section is defined.
    def install_vxace
      return if @installed
      @installed = true
      watch_self_switches
      Static.hook
      Scene_Map.class_eval do
        alias_method :hardrpg_rescue_post_transfer, :post_transfer
        def post_transfer
          HardRPGRescue.take('transfer', [201])
          hardrpg_rescue_post_transfer
        end
        alias_method :hardrpg_rescue_pre_battle_scene, :pre_battle_scene
        def pre_battle_scene
          hardrpg_rescue_pre_battle_scene
          HardRPGRescue.take('battle', [], true)
        end
      end
      DataManager.singleton_class.class_eval do
        alias_method :hardrpg_rescue_save_game_without_rescue, :save_game_without_rescue
        def save_game_without_rescue(*args)
          result = hardrpg_rescue_save_game_without_rescue(*args)
          HardRPGRescue.saved if result
          result
        end
      end
      if @pending
        SceneManager.singleton_class.class_eval do
          alias_method :hardrpg_rescue_first_scene_class, :first_scene_class
          def first_scene_class
            HardRPGRescue.pending && HardRPGRescue.resume_vxace ? Scene_Map : hardrpg_rescue_first_scene_class
          end
        end
      end
      log('on (VX Ace)')
    rescue NameError => e
      @failures = 3
      log("off: #{e.message[0, 160]}")
    end

    # XP and VX have no point between defining the scripts and running them;
    # Graphics.update installs the hooks on the first frame where the game's
    # classes exist (the title, in practice).
    def install_legacy
      return true if @installed
      return false unless defined?(Scene_Map) && defined?(Spriteset_Map) && defined?(Game_Player)
      @installed = true
      watch_self_switches
      Static.hook
      # A transfer marks itself; the next map sprites are built while the
      # screen is dark (VX) or frozen (XP), and the copy is made then.
      if RGSS == 1
        Scene_Map.class_eval do
          alias_method :hardrpg_rescue_transfer_player, :transfer_player
          def transfer_player(*args)
            HardRPGRescue.transferring = true
            hardrpg_rescue_transfer_player(*args)
          ensure
            HardRPGRescue.transferring = false
          end
        end
      else
        Game_Player.class_eval do
          alias_method :hardrpg_rescue_perform_transfer, :perform_transfer
          def perform_transfer(*args)
            HardRPGRescue.transferring = true if transfer?
            hardrpg_rescue_perform_transfer(*args)
          end
        end
      end
      Spriteset_Map.class_eval do
        alias_method :hardrpg_rescue_initialize, :initialize
        def initialize(*args)
          if HardRPGRescue.transferring && ($scene.is_a?(Scene_Map) rescue false)
            HardRPGRescue.transferring = false
            HardRPGRescue.take_transfer
          end
          hardrpg_rescue_initialize(*args)
        end
      end
      Scene_Map.class_eval do
        if method_defined?(:call_battle)
          alias_method :hardrpg_rescue_call_battle, :call_battle
          def call_battle(*args)
            HardRPGRescue.take('battle', [], true)
            hardrpg_rescue_call_battle(*args)
          end
        end
      end
      save_class = RGSS == 1 ? (defined?(Scene_Save) && Scene_Save) : (defined?(Scene_File) && Scene_File)
      if save_class && save_class.method_defined?(:write_save_data)
        save_class.class_eval do
          alias_method :hardrpg_rescue_write_save_data, :write_save_data
          def write_save_data(*args)
            result = hardrpg_rescue_write_save_data(*args)
            HardRPGRescue.saved
            result
          end
        end
      end
      log("on (RGSS#{RGSS})")
      true
    rescue NameError => e
      @failures = 3
      log("off: #{e.message[0, 160]}")
      true
    end

    def take_transfer
      @transfer_music = true
      take('transfer')
    ensure
      @transfer_music = false
    end

    # Scenes a game may still switch to on its own right after a resume
    # (a splash that ends by opening the title): the map is put back.
    STARTUP_SCENE = /title|splash|logo|intro|language|opening|warning/i

    def tick
      return if @busy
      return unless install_legacy
      if @resume_guard
        @resume_guard -= 1
        # Done once the resumed map runs (its sprites exist).
        if @resume_guard <= 0 || ($scene.equal?(@resume_scene) && @resume_scene.instance_variable_get(:@spriteset))
          @resume_guard = nil
        elsif $scene && !$scene.equal?(@resume_scene) && $scene.class.name.to_s =~ STARTUP_SCENE
          log("#{$scene.class} replaced the resumed map; map put back")
          $scene = @resume_scene
        end
        return
      end
      return unless @pending
      return unless $data_system && $data_common_events && $scene
      @busy = true
      begin
        resume_legacy
      ensure
        @busy = false
      end
    end

    def legacy_done?
      @installed && !@pending && !@resume_guard
    end
  end
end

HardRPGRescue.boot
if HardRPGRescue.ready?
  if HardRPGRescue::RGSS == 3
    # define_method, not `def` inside `module Kernel`: the engine evaluates postload
    # scripts from inside rgss_main, and they must keep the top-level namespace
    # (with `def` there, `module SceneManager` in a postload makes Kernel::SceneManager).
    Kernel.send(:alias_method, :hardrpg_rescue_rgss_main, :rgss_main)
    Kernel.send(:private, :hardrpg_rescue_rgss_main)
    Kernel.send(:define_method, :rgss_main) do |*args, &block|
      HardRPGRescue.install_vxace
      hardrpg_rescue_rgss_main(*args, &block)
    end
    Kernel.send(:private, :rgss_main)
  else
    Graphics.singleton_class.send(:alias_method, :hardrpg_rescue_update, :update)
    Graphics.singleton_class.send(:define_method, :update) do |*args|
      HardRPGRescue.tick unless HardRPGRescue.legacy_done?
      hardrpg_rescue_update(*args)
    end
  end
end
