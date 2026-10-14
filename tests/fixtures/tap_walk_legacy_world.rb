# Original services for exercising XP/VX stock player classes in a small map.
module LegacyMapServices
  attr_accessor :units
  def passable?(x,y,*)
    valid?(x,y) && !@walls.include?([x,y])
  end
  def round_x(x); x; end
  def round_y(y); y; end
  def set_display_pos(x,y)
    @display_x = [[x,0].max,[(width-Graphics.width/32.0)*units,0].max].min
    @display_y = [[y,0].max,[(height-Graphics.height/32.0)*units,0].max].min
  end
  def adjust_x(x); x-display_x; end
  def adjust_y(y); y-display_y; end
  def terrain_tag(*); 0; end
  def scroll_down(n); set_display_pos(display_x,display_y+n); end
  def scroll_up(n); set_display_pos(display_x,display_y-n); end
  def scroll_right(n); set_display_pos(display_x+n,display_y); end
  def scroll_left(n); set_display_pos(display_x-n,display_y); end
end
module LegacyEventServices
  def starting; starts > 0; end
  def trigger; 1; end
  def jumping?; false; end
  def over_trigger?; true; end
  def through; true; end
  def character_name; ''; end
  def priority_type; 0; end
end
module LegacyPartyServices
  attr_reader :slip_checks
  def check_map_slip_damage; @slip_checks = (@slip_checks || 0)+1; end
end
module LegacyVehicleServices
  def pos?(*); false; end
end

def setup_legacy_walk_world(data, kind)
  Game_Map.prepend(LegacyMapServices) unless Game_Map.ancestors.include?(LegacyMapServices)
  WalkEvent.prepend(LegacyEventServices) unless WalkEvent.ancestors.include?(LegacyEventServices)
  WalkParty.prepend(LegacyPartyServices) unless WalkParty.ancestors.include?(LegacyPartyServices)
  WalkVehicle.prepend(LegacyVehicleServices) unless WalkVehicle.ancestors.include?(LegacyVehicleServices)
  $game_map = Game_Map.new(data)
  $game_map.units = kind == :xp ? 128 : 256
  $game_party, $game_message = WalkParty.new, WalkMessage.new
  $game_system = Struct.new(:map_interpreter).new($game_map.interpreter)
  $game_temp = Struct.new(:message_window_showing,:player_transferring).new(false,false)
  $game_player = Game_Player.new
  $game_player.moveto(2,4)
  $game_player.instance_variable_set(:@encounter_count,100)
  $scene = Scene_Map.new
end
