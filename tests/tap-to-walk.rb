require_relative '../launcher/vita_tap_to_walk'

# Standalone assertions keep this probe runnable with the bundled Vita Ruby
# and the host Ruby without installing a test framework.
module WalkAssertions
  def assert(value, message = 'assertion failed'); raise message unless value; end
  def refute(value); assert(!value); end
  def assert_equal(expected, actual); assert(expected == actual, "expected #{expected.inspect}, got #{actual.inspect}"); end
  def assert_empty(value); assert(value.empty?, "expected empty, got #{value.inspect}"); end
  def assert_operator(left, operator, right); assert(left.send(operator, right)); end
  def flunk(message); raise message; end
end

class WalkMap
  attr_accessor :map_id, :display_x, :display_y, :walls, :obstacles
  attr_reader :width, :height
  def initialize(width = 16, height = 12, loops = false)
    @width, @height, @loops = width, height, loops
    @map_id, @display_x, @display_y = 1, 0, 0
    @walls, @obstacles = [], []
  end
  def valid?(x, y); x >= 0 && y >= 0 && x < width && y < height; end
  def loop_horizontal?; @loops; end
  def loop_vertical?; @loops; end
  def round_x_with_direction(x, d)
    n = x + (d == 6 ? 1 : d == 4 ? -1 : 0)
    @loops ? n % width : n
  end
  def round_y_with_direction(y, d)
    n = y + (d == 2 ? 1 : d == 8 ? -1 : 0)
    @loops ? n % height : n
  end
end

class WalkPlayer
  attr_accessor :x, :y, :moving, :movable, :reject
  attr_reader :steps, :calls
  def initialize(map, x = 2, y = 4)
    @map, @x, @y, @moving, @movable = map, x, y, false, true
    @steps, @calls = [], 0
  end
  def moving?; @moving; end
  def movable?; @movable; end
  def passable?(x, y, d)
    @calls += 1
    nx, ny = @map.round_x_with_direction(x, d), @map.round_y_with_direction(y, d)
    @map.valid?(nx, ny) && !@map.walls.include?([nx, ny]) &&
      !@map.obstacles.include?([nx, ny]) && !@map.walls.include?([x, y])
  end
  def move_straight(d)
    return if @reject || !passable?(@x, @y, d)
    @x, @y = @map.round_x_with_direction(x, d), @map.round_y_with_direction(y, d)
    @steps << [@x, @y]
  end
end

class TapToWalkTest
  include WalkAssertions
  def setup
    @map, @driver = WalkMap.new, HardRPGTapWalk::Driver.new
    @player = WalkPlayer.new(@map)
    @now = 0.0
  end
  def tick(options = {})
    @now += 0.016
    @driver.tick(@map, @player, {:now => @now}.merge(options))
  end
  def finish
    200.times do
      tick
      return unless @driver.active?
    end
    flunk 'route did not finish within bounded fixture frames'
  end
  def first_step
    20.times do
      tick
      return unless @player.steps.empty?
    end
    flunk 'ordinary movement did not start'
  end
  def test_detour_uses_ordinary_steps_and_never_crosses_walls
    @map.walls = (1..8).map { |y| [7, y] }
    @driver.request(@map, @player, [12, 4])
    finish
    assert_equal [12, 4], [@player.x, @player.y]
    assert_equal :arrived, @driver.status
    assert_equal 18, @player.steps.size # shortest detour above the wall
    assert_empty @player.steps & @map.walls
  end
  def test_unreachable_destination_does_not_move
    @map.walls = [[12, 3], [11, 4], [13, 4], [12, 5]]
    @driver.request(@map, @player, [12, 4])
    finish
    assert_equal :unreachable, @driver.reason
    assert_empty @player.steps
  end
  def test_blocked_tile_and_outside_tap_do_not_move
    @map.walls = [[4, 4]]
    @driver.request(@map, @player, [4, 4]); finish
    assert_empty @player.steps
    refute @driver.request_pixel(@map, @player, -1, 4, 544, 416)
    refute @driver.request_pixel(@map, @player, 544, 4, 544, 416)
    assert_equal :outside, @driver.reason
  end
  def test_fractional_camera_offsets_and_looping_coordinates
    @map.display_x, @map.display_y = 1.5, 2.75
    @driver.request_pixel(@map, @player, 80, 48, 544, 416)
    assert_equal [4, 4], @driver.target
    @map = WalkMap.new(16, 12, true); @player = WalkPlayer.new(@map)
    @map.display_x = 15.5
    @driver.request_pixel(@map, @player, 32, 0, 544, 416)
    assert_equal [0, 0], @driver.target
  end
  def test_looping_map_takes_short_wraparound_path
    @map = WalkMap.new(16, 12, true); @player = WalkPlayer.new(@map, 0, 4)
    @driver.request(@map, @player, [15, 4]); finish
    assert_equal [[15, 4]], @player.steps
  end
  def test_any_manual_direction_cancels_before_another_automatic_step
    @driver.request(@map, @player, [12, 4]); first_step
    before = @player.steps.dup
    refute tick(:physical => true)
    10.times { tick }
    assert_equal before, @player.steps
    assert_equal :physical, @driver.reason
  end
  def test_manual_direction_cancels_while_a_tile_step_is_animating
    @driver.request(@map, @player, [12, 4]); first_step
    @player.moving = true
    refute tick(:physical => true)
    assert_equal :physical, @driver.reason
    refute @driver.active?
  end
  def test_a_new_tap_replaces_the_route
    @driver.request(@map, @player, [12, 4]); tick
    @driver.request(@map, @player, [3, 8]); finish
    assert_equal [3, 8], [@player.x, @player.y]
  end
  def test_dialogue_or_menu_cancels_and_returns_original_input
    @driver.request(@map, @player, [12, 4]); tick
    refute tick(:blocked => true)
    refute @driver.active?
  end
  def test_transfer_scene_or_teleport_drops_stale_route
    @driver.request(@map, @player, [12, 4]); tick
    @map.map_id = 2
    refute tick
    assert_equal :context_changed, @driver.reason
    @driver.request(@map, @player, [12, 4])
    @player.x = 9
    refute tick
    assert_equal :context_changed, @driver.reason
  end
  def test_long_pause_or_clock_reversal_cancels
    @driver.request(@map, @player, [12, 4]); tick
    @now += 1
    refute tick
    assert_equal :time_gap, @driver.reason
    @driver.request(@map, @player, [12, 4]); @now -= 2
    refute tick
    assert_equal :time_gap, @driver.reason
  end
  def test_moving_obstacle_replans_without_walking_through_it
    @driver.request(@map, @player, [12, 4]); tick
    @map.obstacles = [[@player.x + 1, @player.y]]
    finish
    assert_equal [12, 4], [@player.x, @player.y]
    assert_empty @player.steps & @map.obstacles
  end
  def test_obstruction_retries_are_bounded
    @driver.request(@map, @player, [12, 4]); first_step
    3.times do
      @map.obstacles = [[@player.x + 1, @player.y]]
      tick
      @map.obstacles = []
      @driver.search.advance(1000) if @driver.search
      # Obstruct the first edge of the next planned route.
    end
    assert_equal :obstructed, @driver.reason
    refute @driver.active?
  end
  def test_normal_movement_rejection_cancels
    @player.reject = true
    @driver.request(@map, @player, [12, 4]); finish
    assert_equal :movement_rejected, @driver.reason
  end
  def test_search_has_per_frame_and_total_work_limits
    @map = WalkMap.new(80, 80); @player = WalkPlayer.new(@map)
    search = HardRPGTapWalk::Search.new(@map, @player, [79, 79], 100)
    assert_equal :searching, search.advance(8)
    assert_equal 8, search.expanded
    20.times { search.advance(8) }
    assert_equal :limit, search.status
    assert_equal 100, search.expanded
  end
  def test_wall_tap_selects_nearest_reachable_tile
    @map.walls = [[7,4]]
    @driver.request_pixel(@map,@player,240,144,544,416)
    finish
    assert_equal [6,4], [@player.x,@player.y]
    assert_equal [6,4], @driver.target
    assert_empty @player.steps & @map.walls
  end
  def test_tolerance_still_prefers_exact_reachable_destination
    @driver.request_pixel(@map,@player,240,144,544,416)
    finish
    assert_equal [7,4], [@player.x,@player.y]
  end
  def test_enclosed_target_selects_nearby_reachable_floor
    @map.walls = [[12,3],[11,4],[13,4],[12,5]]
    @driver.request_pixel(@map,@player,400,144,544,416)
    finish
    dx,dy = @player.x-12,@player.y-4
    assert_equal 2, dx*dx+dy*dy
    assert_empty @player.steps & @map.walls
  end
  def test_tap_just_outside_map_snaps_to_edge_but_far_tap_is_ignored
    @map = WalkMap.new(8,8); @player = WalkPlayer.new(@map)
    @driver.request_pixel(@map,@player,272,144,544,416)
    finish
    assert_equal [7,4], [@player.x,@player.y]
    @driver.request_pixel(@map,@player,400,144,544,416)
    assert_equal :outside, @driver.reason
    refute @driver.active?
  end
  def test_tap_beyond_tolerance_never_selects_arbitrary_distant_floor
    @map.walls = (4..10).flat_map { |x| (1..7).map { |y| [x,y] } }
    @driver.request_pixel(@map,@player,240,144,544,416)
    finish
    assert_equal :unreachable, @driver.reason
    assert_empty @player.steps
  end
  def test_tolerant_search_work_and_replans_keep_original_tap_radius
    @driver.request(@map,@player,[12,4],2)
    first_step
    # Replanning must retain the tap rather than growing a new radius around
    # each fallback destination.
    assert_equal [12,4], @driver.search.instance_variable_get(:@target)
    @map.obstacles = [[@player.x+1,@player.y]]
    tick
    assert_equal [12,4], @driver.search.instance_variable_get(:@target)
    finish
    assert_equal [12,4], [@player.x,@player.y]
  end
  def test_astar_routes_match_reference_shortest_paths_in_seeded_mazes
    random = Random.new(42)
    25.times do
      map = WalkMap.new
      map.walls = (0...map.width).flat_map do |x|
        (0...map.height).filter_map do |y|
          [x,y] if ![[2,4],[12,8]].include?([x,y]) && random.rand < 0.25
        end
      end
      player = WalkPlayer.new(map)
      queue, distances = [[2,4]], {[2,4]=>0}
      queue.each do |here|
        HardRPGTapWalk::DIRECTIONS.each do |d|
          next unless player.passable?(*here,d)
          there = [map.round_x_with_direction(here[0],d),map.round_y_with_direction(here[1],d)]
          next if distances.has_key?(there)
          distances[there] = distances[here]+1
          queue << there
        end
      end
      search = HardRPGTapWalk::Search.new(map,player,[12,8])
      search.advance(1024)
      expected = distances[[12,8]]
      if expected
        assert_equal :found, search.status
        assert_equal expected, search.route.size
      else
        assert_equal :unreachable, search.status
      end
    end
  end
end

tests = TapToWalkTest.instance_methods.grep(/^test_/).sort
tests.each do |name|
  test = TapToWalkTest.new
  test.setup
  test.send(name)
  puts "PASS: #{name}"
end
puts "#{tests.size} tap-to-walk tests passed"
