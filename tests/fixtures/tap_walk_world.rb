# Original test world supplying map/party services to the VX Ace player API.
class WalkInterpreter
  attr_accessor :running
  def running?; !!@running; end
end
class WalkEvent
  attr_reader :x, :y, :starts
  def initialize(x, y, solid = true)
    @x, @y, @solid, @starts = x, y, solid, 0
  end
  def normal_priority?; @solid; end
  def trigger_in?(triggers); triggers.include?(1); end
  def start; @starts += 1; end
end
class WalkVehicle
  def pos_nt?(*); false; end
end
class Game_Followers
  attr_reader :moves
  def initialize(*); @moves = 0; end
  def move; @moves += 1; end
  def gathering?; false; end
  def collide?(*); false; end
  def synchronize(*); end
  def update; end
end
class Game_Event; end
class Game_Map
  attr_reader :width, :height, :interpreter, :events, :tile_checks
  attr_accessor :display_x, :display_y, :map_id, :walls
  def initialize(data)
    @width, @height = data['width'], data['height']
    @walls, @map_id = data['walls'], 1
    @display_x, @display_y, @tile_checks = 0.0, 0.0, 0
    @interpreter = WalkInterpreter.new
    @events = {1 => WalkEvent.new(12, 4, false)}
  end
  def valid?(x, y); x >= 0 && y >= 0 && x < width && y < height; end
  def loop_horizontal?; false; end
  def loop_vertical?; false; end
  def round_x(x); x; end
  def round_y(y); y; end
  def x_with_direction(x, d); x + (d == 6 ? 1 : d == 4 ? -1 : 0); end
  def y_with_direction(y, d); y + (d == 2 ? 1 : d == 8 ? -1 : 0); end
  alias round_x_with_direction x_with_direction
  alias round_y_with_direction y_with_direction
  def passable?(x, y, d)
    @tile_checks += 1
    valid?(x, y) && !@walls.include?([x, y])
  end
  def events_xy_nt(x, y); @events.values.select { |e| e.x == x && e.y == y }; end
  alias events_xy events_xy_nt
  def boat; @boat ||= WalkVehicle.new; end
  alias ship boat
  def vehicle(*); nil; end
  def set_display_pos(x, y)
    @display_x = [[x, 0].max, [width - Graphics.width / 32.0, 0].max].min
    @display_y = [[y, 0].max, [height - Graphics.height / 32.0, 0].max].min
  end
  def adjust_x(x); x - @display_x; end
  def adjust_y(y); y - @display_y; end
  def disable_dash?; false; end
  def encounter_step; 100; end
  def bush?(*); false; end
  def ladder?(*); false; end
  def damage_floor?(*); false; end
  def counter?(*); false; end
  def scroll_down(n); set_display_pos(@display_x, @display_y + n); end
  def scroll_up(n); set_display_pos(@display_x, @display_y - n); end
  def scroll_left(n); set_display_pos(@display_x - n, @display_y); end
  def scroll_right(n); set_display_pos(@display_x + n, @display_y); end
  def setup_starting_event; false; end
  def any_event_starting?; false; end
end
class WalkParty
  attr_reader :steps, :walks
  def initialize; @steps = @walks = 0; end
  def increase_steps; @steps += 1; end
  def on_player_walk; @walks += 1; end
  def in_battle; false; end
  def encounter_none?; false; end
  def encounter_half?; false; end
end
class WalkMessage
  attr_accessor :visible, :busy
  def busy?; !!@busy; end
end
class Scene_Map
  def terminate; @terminated = true; end
end
module SceneManager
  class << self
    attr_accessor :scene
  end
end

def setup_walk_world(data)
  $game_map = Game_Map.new(data)
  $game_party = WalkParty.new
  $game_message = WalkMessage.new
  $data_system = Struct.new(:opt_transparent).new(false)
  $game_player = Game_Player.new
  $game_player.moveto(2, 4)
  $game_player.instance_variable_set(:@encounter_count, 100)
  SceneManager.scene = Scene_Map.new
end
