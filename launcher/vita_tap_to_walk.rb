# Opt-in adapters for stock XP, VX and VX Ace tile movement. Load after game classes,
# then call HardRPGTapWalk.install! explicitly for a compatible game.
module HardRPGTapWalk
  DIRECTIONS = [2, 4, 6, 8].freeze
  NODES_PER_FRAME = 64
  MAX_NODES = 1024
  MAX_REPLANS = 2
  TAP_RADIUS = 2

  class MapAdapter
    attr_reader :raw, :units
    def initialize(raw, units)
      @raw, @units = raw, units
    end
    def map_id; raw.map_id; end
    def width; raw.width; end
    def height; raw.height; end
    def display_x; raw.display_x / units.to_f; end
    def display_y; raw.display_y / units.to_f; end
    def valid?(x,y); raw.valid?(x,y); end
    def loop_horizontal?; raw.respond_to?(:loop_horizontal?) && raw.loop_horizontal?; end
    def loop_vertical?; raw.respond_to?(:loop_vertical?) && raw.loop_vertical?; end
    def round_x_with_direction(x,d)
      x += d == 6 ? 1 : d == 4 ? -1 : 0
      loop_horizontal? ? x % width : x
    end
    def round_y_with_direction(y,d)
      y += d == 2 ? 1 : d == 8 ? -1 : 0
      loop_vertical? ? y % height : y
    end
    def adjust_x(x)
      raw.respond_to?(:adjust_x) ? raw.adjust_x(x * units) / units.to_f : x - display_x
    end
    def adjust_y(y)
      raw.respond_to?(:adjust_y) ? raw.adjust_y(y * units) / units.to_f : y - display_y
    end
  end

  class PlayerAdapter
    attr_reader :raw, :engine
    def initialize(raw, engine)
      @raw, @engine = raw, engine
    end
    def x; raw.x; end
    def y; raw.y; end
    def moving?; raw.moving?; end
    def movable?
      engine.kind == :xp ? !engine.blocked?(raw) && !raw.moving? : raw.movable?
    end
    def passable?(x,y,d)
      if engine.kind == :vx
        map = engine.map_for($game_map)
        raw.passable?(map.round_x_with_direction(x,d), map.round_y_with_direction(y,d))
      else
        raw.passable?(x,y,d)
      end
    end
    def move_straight(d)
      if engine.kind == :ace
        raw.move_straight(d)
      else
        raw.send({2=>:move_down,4=>:move_left,6=>:move_right,8=>:move_up}.fetch(d))
      end
    end
  end

  class EngineAdapter
    attr_reader :kind
    def initialize(kind)
      raise 'Unknown tap-to-walk engine' unless [:xp,:vx,:ace].include?(kind)
      @kind = kind
    end
    def map_for(raw)
      units = {:xp=>128,:vx=>256,:ace=>1}.fetch(kind)
      @map = MapAdapter.new(raw,units) unless @map && @map.raw.equal?(raw)
      @map
    end
    def player_for(raw)
      @player = PlayerAdapter.new(raw,self) unless @player && @player.raw.equal?(raw)
      @player
    end
    def blocked?(player)
      scene = kind == :ace ? SceneManager.scene : $scene
      return true unless scene.is_a?(Scene_Map)
      return true if player.move_route_forcing
      if kind == :xp
        return $game_system.map_interpreter.running? ||
               $game_temp.message_window_showing || $game_temp.player_transferring
      end
      return true if $game_map.interpreter.running? || player.transfer?
      return true if $game_message.respond_to?(:visible) && $game_message.visible
      return true if $game_message.respond_to?(:busy?) && $game_message.busy?
      kind == :ace ? !player.normal_walk? : player.in_vehicle?
    end
  end

  class Search
    attr_reader :status, :route, :expanded, :destination
    def initialize(map, player, target, limit = MAX_NODES, radius = 0)
      @map, @player, @target, @limit = map, player, target, limit
      @radius, @best, @best_distance = radius, nil, nil
      start = [player.x, player.y]
      @parents, @costs, @closed = {start => nil}, {start => 0}, {}
      @heap, @serial, @expanded = [], 0, 0
      push(start, 0)
      @status, @route = :searching, []
    end
    def advance(budget = NODES_PER_FRAME)
      budget.times do
        return @status unless @status == :searching
        if @heap.empty?
          finish(:unreachable)
          break
        end
        entry = pop
        here, cost = entry[2], entry[3]
        next if @closed[here] || cost != @costs[here]
        dx, dy = (here[0] - @target[0]).abs, (here[1] - @target[1]).abs
        dx = [dx, @map.width - dx].min if @map.loop_horizontal?
        dy = [dy, @map.height - dy].min if @map.loop_vertical?
        distance = dx * dx + dy * dy
        if distance <= @radius * @radius &&
           (@best_distance.nil? || distance < @best_distance ||
            (distance == @best_distance && cost < @costs[@best]))
          @best, @best_distance = here, distance
        end
        if distance == 0
          finish(:unreachable)
          break
        end
        if @expanded >= @limit
          finish(:limit)
          break
        end
        @expanded += 1
        @closed[here] = true
        DIRECTIONS.each do |direction|
          next unless @player.passable?(here[0], here[1], direction)
          there = [@map.round_x_with_direction(here[0], direction),
                   @map.round_y_with_direction(here[1], direction)]
          next unless @map.valid?(*there)
          next if @closed[there]
          next if @costs.has_key?(there) && @costs[there] <= cost + 1
          @costs[there] = cost + 1
          @parents[there] = [here, direction]
          push(there, cost + 1)
        end
      end
      @status
    end
    def push(tile, cost)
      dx, dy = (tile[0] - @target[0]).abs, (tile[1] - @target[1]).abs
      dx = [dx, @map.width - dx].min if @map.loop_horizontal?
      dy = [dy, @map.height - dy].min if @map.loop_vertical?
      entry = [cost + dx + dy, @serial, tile, cost]
      @serial += 1
      index = @heap.size
      @heap << entry
      while index > 0
        parent = (index - 1) / 2
        break if (@heap[parent] <=> entry) <= 0
        @heap[index] = @heap[parent]
        index = parent
      end
      @heap[index] = entry
    end
    def pop
      first = @heap.first
      last = @heap.pop
      unless @heap.empty?
        index = 0
        while (left = index * 2 + 1) < @heap.size
          right = left + 1
          child = right < @heap.size && (@heap[right] <=> @heap[left]) < 0 ? right : left
          break if (last <=> @heap[child]) <= 0
          @heap[index] = @heap[child]
          index = child
        end
        @heap[index] = last
      end
      first
    end
    def finish(failure)
      unless @best
        @status = failure
        return
      end
      @destination = @best
      cursor = @best
      while (parent = @parents[cursor])
        @route << parent[1]
        cursor = parent[0]
      end
      @route.reverse!
      @status = :found
    end
  end

  class Driver
    attr_reader :status, :target, :route, :search, :reason
    def initialize
      @status, @route, @last_tick = :idle, [], nil
    end
    def cancel(reason = :cancelled)
      @status, @reason = :idle, reason
      @target, @requested, @search, @context, @expected = nil, nil, nil, nil, nil
      @route = []
    end
    def request(map, player, tile, radius = 0)
      cancel(:new_destination)
      dx = tile[0] < 0 ? -tile[0] : [tile[0] - map.width + 1, 0].max
      dy = tile[1] < 0 ? -tile[1] : [tile[1] - map.height + 1, 0].max
      return @reason = :outside if dx * dx + dy * dy > radius * radius
      @target, @requested, @radius = tile, tile, radius
      @context = [map.object_id, map.map_id, player.object_id]
      @expected = [player.x, player.y]
      @replans = 0
      replan(map, player)
    end
    def request_pixel(map, player, x, y, width, height)
      unless x >= 0 && y >= 0 && x < width && y < height
        cancel(:outside)
        return false
      end
      # Input.mouse_x/y already account for letterboxing and runtime scaling.
      tile = [(map.display_x + x / 32.0).floor,
              (map.display_y + y / 32.0).floor]
      tile[0] %= map.width if map.loop_horizontal?
      tile[1] %= map.height if map.loop_vertical?
      request(map, player, tile, TAP_RADIUS)
      true
    end
    def active?
      @status == :planning || @status == :walking
    end
    def replan(map, player)
      @search = Search.new(map, player, @requested, MAX_NODES, @radius)
      @route, @status = [], :planning
    end
    def tick(map, player, options = {})
      now = options.fetch(:now) { Time.now.to_f }
      gap = @last_tick && (now < @last_tick || now - @last_tick > 0.25)
      @last_tick = now
      if options[:physical] || options[:cancel] || options[:blocked] || gap
        cancel(options[:physical] ? :physical : gap ? :time_gap : :blocked)
        return false
      end
      return false unless active?
      if @context != [map.object_id, map.map_id, player.object_id] ||
         @expected != [player.x, player.y]
        cancel(:context_changed)
        return false
      end
      return true if player.moving?
      unless player.movable?
        cancel(:not_movable)
        return false
      end
      if @status == :planning
        result = @search.advance
        if result == :found
          @target = @search.destination
          @route, @status = @search.route.dup, :walking
        elsif result != :searching
          cancel(result)
          return true
        else
          return true
        end
      end
      if @route.empty?
        @status, @reason = :arrived, :arrived
        return true
      end
      direction = @route.first
      unless player.passable?(player.x, player.y, direction)
        @replans += 1
        if @replans > MAX_REPLANS
          cancel(:obstructed)
        else
          replan(map, player)
        end
        return true
      end
      next_tile = [map.round_x_with_direction(player.x, direction),
                   map.round_y_with_direction(player.y, direction)]
      # Normal movement owns collisions, touch events, followers and steps.
      player.move_straight(direction)
      if [player.x, player.y] != next_tile
        cancel(:movement_rejected)
        return true
      end
      @route.shift
      @expected = next_tile
      true
    end
  end

  class DestinationMarker
    attr_reader :sprite
    def initialize(viewport = nil)
      @viewport = viewport
    end
    def update(map, state)
      return if @disposed
      unless state.active? && state.target
        @sprite.visible = false if @sprite
        @phase, @target = 0, nil
        return
      end
      unless @sprite
        @bitmap = Bitmap.new(32, 32)
        @bitmap.fill_rect(0, 0, 32, 32, Color.new(255, 255, 255))
        @sprite = Sprite.new(@viewport)
        @sprite.bitmap, @sprite.z = @bitmap, 180
        @sprite.ox, @sprite.oy, @sprite.blend_type = 16, 16, 1
      end
      if @target != state.target
        @phase, @target = 0, state.target.dup
      end
      @phase = (@phase + 1) % 20
      @sprite.visible = true
      @sprite.x = (map.adjust_x(state.target[0]) + 0.5) * 32
      @sprite.y = (map.adjust_y(state.target[1]) + 0.5) * 32
      @sprite.opacity = (20 - @phase) * 6
      @sprite.zoom_x = @sprite.zoom_y = 1.0 + @phase / 20.0
    end
    def dispose
      return if @disposed
      @sprite.dispose if @sprite
      @bitmap.dispose if @bitmap
      @sprite, @bitmap, @disposed = nil, nil, true
    end
  end

  class << self
    attr_reader :enabled, :engine
    def enabled=(value)
      driver.cancel(:disabled) unless value
      @enabled = value
    end
    def driver
      @driver ||= Driver.new
    end
    def handle(player)
      return false unless enabled
      map = engine.map_for($game_map)
      blocked = engine.blocked?(player)
      physical = Input.dir4 != 0
      cancel = Input.trigger?(:B) || Input.trigger?(:MOUSERIGHT)
      if !physical && !cancel && !blocked && Input.trigger?(:MOUSELEFT) &&
         Input.mouse_in_window?
        driver.request_pixel(map, engine.player_for(player), Input.mouse_x, Input.mouse_y,
                             Graphics.width, Graphics.height)
      end
      driver.tick(map, engine.player_for(player), :physical => physical, :cancel => cancel,
                  :blocked => blocked)
    end
    def install!(kind = nil)
      unless defined?(Game_Player)
        raise 'Tap-to-walk requires game classes to be loaded first'
      end
      kind ||= Game_Player.method_defined?(:normal_walk?) ? :ace :
               Game_Player.method_defined?(:move_by_input) ? :vx : :xp
      @engine = EngineAdapter.new(kind)
      required = [:passable?, :moving?, :move_route_forcing]
      required += kind == :ace ? [:move_by_input,:move_straight,:movable?,:transfer?,:normal_walk?] :
                  kind == :vx ? [:move_by_input,:move_down,:move_left,:move_right,:move_up,:movable?,:transfer?,:in_vehicle?] :
                                [:update,:move_down,:move_left,:move_right,:move_up]
      unless defined?(Game_Player) && defined?(Scene_Map) &&
             required.all? { |name| Game_Player.method_defined?(name) }
        raise 'Tap-to-walk requires stock tile movement methods for the selected engine'
      end
      input = kind == :xp ? LegacyPlayerInput : PlayerInput
      Game_Player.prepend(input) unless Game_Player.ancestors.include?(input)
      lifecycle = Scene_Map.method_defined?(:terminate) ? MapLifecycle : LegacyMapLifecycle
      Scene_Map.prepend(lifecycle) unless Scene_Map.ancestors.include?(lifecycle)
      if defined?(Spriteset_Map) && !Spriteset_Map.ancestors.include?(MapSprites)
        Spriteset_Map.prepend(MapSprites)
      end
      driver.cancel(:installed)
      self.enabled = true
    end
  end
  module PlayerInput
    def move_by_input
      super unless HardRPGTapWalk.handle(self)
    end
  end
  module LegacyPlayerInput
    def update
      HardRPGTapWalk.handle(self)
      super
    end
  end
  module MapLifecycle
    def terminate
      HardRPGTapWalk.driver.cancel(:scene_changed)
      super
    end
  end
  module LegacyMapLifecycle
    def main
      super
    ensure
      HardRPGTapWalk.driver.cancel(:scene_changed)
    end
  end
  module MapSprites
    def initialize
      super
      @hardrpg_destination_marker = DestinationMarker.new(@viewport1)
    end
    def update
      super
      if @hardrpg_destination_marker
        map = HardRPGTapWalk.engine ? HardRPGTapWalk.engine.map_for($game_map) : $game_map
        @hardrpg_destination_marker.update(map, HardRPGTapWalk.driver)
      end
    end
    def dispose
      @hardrpg_destination_marker.dispose if @hardrpg_destination_marker
      super
    end
  end
end
