# Diagnostic postload script. Configuration and outputs stay outside game data.
module PocketMirrorMemory
  CONFIG = Marshal.load(File.binread('app0:/issue19-config.marshal'))
  ROOT = CONFIG.fetch('output')
  TARGET = '[FIXED] sombra corridorrrr'
  class << self
    def clock
      Process.clock_gettime(Process::CLOCK_MONOTONIC)
    end
    def log(message)
      File.open(ROOT + '/progress.txt', 'a') { |f| f.puts('%.3f %s' % [clock, message]) }
    end
    def snapshot(label)
      cache = Cache.instance_variable_get(:@cache)
      log("MEM #{label} #{System.vita_memory_snapshot} gc=#{GC.count} cache=#{cache ? cache.size : -1}")
    end
    def survey
      infos = load_data('Data/MapInfos.rvdata2')
      result = { 'target' => TARGET, 'maps' => [], 'inbound' => [] }
      transfers = []
      infos.keys.sort.each_with_index do |id, index|
        map = load_data('Data/Map%03d.rvdata2' % id)
        matches = []
        map.events.each_value do |event|
          event.pages.each_with_index do |page, page_index|
            page.list.each_with_index do |command, command_index|
              args = command.parameters
              if command.code == 231 && args[1].to_s.downcase == TARGET.downcase
                matches << { 'event' => event.id, 'name' => event.name, 'x' => event.x,
                  'y' => event.y, 'page' => page_index, 'trigger' => page.trigger,
                  'conditions' => page.condition.inspect, 'command' => command_index,
                  'parameters' => args }
              elsif command.code == 201 && args[0] == 0
                transfers << { 'from' => id, 'event' => event.id, 'x' => event.x,
                  'y' => event.y, 'to' => args[1], 'parameters' => args }
              end
            end
          end
        end
        unless matches.empty?
          row = { 'id' => id, 'name' => infos[id].name, 'width' => map.width,
            'height' => map.height, 'matches' => matches }
          result['maps'] << row
          File.binwrite(ROOT + '/Map%03d.rvdata2' % id, Marshal.dump(map))
          log("MATCH #{row.inspect}")
        end
        Graphics.update if index % 5 == 0
        log("SCAN #{index + 1}/#{infos.size}") if index % 50 == 0
      end
      targets = result['maps'].map { |r| r['id'] }
      result['inbound'] = transfers.select { |t| targets.include?(t['to']) }
      File.binwrite(ROOT + '/survey.marshal', Marshal.dump(result))
      snapshot('survey-end')
      log("DONE survey matches=#{targets.inspect}")
    end
    def save_path(path)
      return path unless path.is_a?(String)
      return path unless path == 'Quick_Save' || path =~ /\ASave\d+\.rvdata2\z/i
      ROOT + '/saves/' + path
    end
    def protect_saves
      Dir.mkdir(ROOT + '/saves') unless File.directory?(ROOT + '/saves')
      File.singleton_class.prepend(Module.new do
        %i[open binread binwrite read write delete exist? file? size].each do |name|
          define_method(name) do |path, *args, &block|
            super(PocketMirrorMemory.save_path(path), *args, &block)
          end
        end
        def rename(from, to)
          super(PocketMirrorMemory.save_path(from), PocketMirrorMemory.save_path(to))
        end
      end)
    end
    def load_fixture
      File.open('app0:/issue19-save.rvdata2', 'rb') do |file|
        Marshal.load(file)
        DataManager.extract_save_contents(Marshal.load(file))
      end
      DataManager.reload_map_if_updated
      $game_system.on_after_load
      log("FIXTURE map=#{$game_map.map_id} xy=#{$game_player.x},#{$game_player.y}")
    end
    def request_map(id)
      @expected_map = id
      @ready_scene = nil
      @phase = :entering
      @frames = 0
      @progress_at = clock
      snapshot("before-map-#{id}")
      # Test shortcuts skip traversal; emulate the old room erasing its pictures,
      # while preserving the real game cache, native pools and normal GC policy.
      $game_map.screen.pictures.each { |picture| picture.erase }
      point = id == 17 ? [45, 61, 8] : CONFIG.fetch('spawns').fetch(id.to_s)
      $game_player.reserve_transfer(id, point[0], point[1], point[2])
      log("TRANSFER index=#{@index} target=#{id} point=#{point.inspect}")
    end
    def ready(scene)
      return unless @active
      @ready_scene = scene
      @frames = 0
      @progress_at = clock
      if scene.is_a?(Scene_Map)
        raise "unexpected map #{$game_map.map_id}, expected #{@expected_map}" unless $game_map.map_id == @expected_map
        @phase = :map
      elsif scene.is_a?(Scene_Menu)
        @phase = :menu
      end
      log("READY #{scene.class} map=#{$game_map.map_id} index=#{@index}")
      snapshot('ready')
    end
    def tick
      return unless @active && !@busy
      raise "probe stalled phase=#{@phase} expected=#{@expected_map} map=#{$game_map.map_id}" if clock - @progress_at > 90
      return unless @ready_scene.equal?(SceneManager.scene)
      entered = true
      @busy = true
      @frames += 1
      if @phase == :map && @frames == 60
        snapshot('map-settled')
        if CONFIG['mode'] == 'cold'
          @active = false
          result('pass', 'cold-entry')
          log('DONE cold-entry')
        elsif CONFIG.fetch('menus', true) && !@menu_done
          @menu_done = true
          @ready_scene = nil
          Window_MenuCommand.init_command_position if Window_MenuCommand.respond_to?(:init_command_position)
          SceneManager.call(Scene_Menu)
        else
          @menu_done = false
          @index += 1
          if @index >= @queue.size
            @active = false
            snapshot('done')
            result('pass', "route visits=#{@index}")
            log("DONE route visits=#{@index}")
          else
            request_map(@queue[@index])
          end
        end
      elsif @phase == :menu && @frames == 30
        @ready_scene = nil
        SceneManager.return
      end
    ensure
      @busy = false if entered
    end
    def scene_run
      load_fixture
      @queue = CONFIG.fetch('maps') * CONFIG.fetch('cycles', 1)
      @index = 0
      @active = true
      request_map(@queue.first)
      $game_player.perform_transfer
      $game_map.autoplay
      SceneManager.goto(Scene_Map)
      while SceneManager.scene
        SceneManager.scene.main
      end
    end
    def finish_requested_transfer(scene)
      return unless @active && @phase == :entering && !@forcing_transfer
      return unless SceneManager.scene.equal?(scene) && $game_player.transfer?
      entered = true
      # Normal update_scene waits for the story interpreter. A stress-route
      # shortcut must not wait indefinitely for dialogue or puzzle input.
      # Use the same pre_transfer/perform_transfer/post_transfer implementation.
      @forcing_transfer = true
      log("ROUTE bypass-story-wait target=#{@expected_map}")
      $game_message.clear
      scene.__send__(:perform_transfer)
    ensure
      @forcing_transfer = false if entered
    end
    def result(status, message)
      File.binwrite(ROOT + '/result.marshal', Marshal.dump({ 'status' => status,
        'message' => message, 'index' => @index, 'map' => ($game_map.map_id rescue nil),
        'time' => clock }))
    end
    def hold(message)
      sprite = Sprite.new
      sprite.bitmap = Bitmap.new(544, 64)
      sprite.bitmap.draw_text(0, 0, 544, 64, message, 1)
      loop { Graphics.update; Input.update }
    end
    def run
      Dir.mkdir(ROOT) unless File.directory?(ROOT)
      # Older engines relaunch a direct game after an error. Retain the first
      # outcome rather than silently starting a fresh process and hiding it.
      if File.exist?(ROOT + '/result.marshal')
        previous = Marshal.load(File.binread(ROOT + '/result.marshal'))
        return hold('Issue 19 retained: ' + previous['status'])
      end
      log("START #{CONFIG.inspect} ruby=#{RUBY_VERSION}")
      protect_saves
      DataManager.init
      snapshot('database-loaded')
      if CONFIG['mode'] != 'survey'
        scene_run
        return
      end
      survey
      # Hold the completed result until the host collects it and closes this title.
      hold('Issue 19: survey complete')
    rescue Exception => error
      log("ERROR #{error.class}: #{error.message}\n#{Array(error.backtrace).first(12).join("\n")}")
      snapshot('failure') rescue nil
      result('error', "#{error.class}: #{error.message}") rescue nil
      raise
    end
  end
end
Scene_Base.prepend(Module.new do
  def post_start
    super
    PocketMirrorMemory.ready(self)
  end
end)
Scene_Map.prepend(Module.new do
  def update
    super
    PocketMirrorMemory.finish_requested_transfer(self)
  end
  def post_transfer
    super
    PocketMirrorMemory.ready(self)
  end
end)
Scene_Menu.prepend(Module.new do
  # Galv's menu replaces post_start without calling Scene_Base's method.
  def post_start
    super
    PocketMirrorMemory.ready(self)
  end
end)
Graphics.singleton_class.prepend(Module.new do
  def update
    result = super
    PocketMirrorMemory.tick
    result
  end
end)
Game_System.prepend(Module.new do
  def clear_memory
    PocketMirrorMemory.snapshot('game-clear-memory-before')
    result = super
    PocketMirrorMemory.snapshot('game-clear-memory-after')
    result
  end
end)
SceneManager.singleton_class.prepend(Module.new do
  def run
    PocketMirrorMemory.run
  end
end)
