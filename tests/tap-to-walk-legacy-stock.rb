# Integration with user-owned stock XP or VX movement classes.
kind, root = ARGV.fetch(0).to_sym, ARGV.fetch(1)
module Graphics
  class << self
    attr_accessor :width, :height
  end
end
Graphics.width, Graphics.height = kind == :xp ? [640,480] : [544,416]
module Input
  A, B, C, CTRL = :A, :B, :C, :CTRL
  class << self
    attr_accessor :direction, :click, :mx, :my
    def dir4; direction || 0; end
    def trigger?(button); button == :MOUSELEFT && !!click; end
    def press?(*); false; end
    def mouse_x; mx || 0; end
    def mouse_y; my || 0; end
    def mouse_in_window?; true; end
  end
end
if kind == :xp
  %w[Game_Character_1 Game_Character_2 Game_Character_3 Game_Player].each do |name|
    path = Dir[File.join(root,"*-#{name}.rb")].fetch(0)
    load path
  end
elsif kind == :vx
  %w[027.rb 030.rb].each { |name| load File.join(root,name) }
else
  raise 'Expected xp or vx'
end
require_relative 'fixtures/tap_walk_world'
require_relative '../launcher/vita_tap_to_walk'
require_relative 'fixtures/tap_walk_legacy_world'
units = kind == :xp ? 128 : 256
world = lambda do
  setup_legacy_walk_world({'width'=>20,'height'=>16,'walls'=>(1..8).map {|y| [7,y]}},kind)
  Input.direction, Input.click = 0,false
  HardRPGTapWalk.install!(kind)
end
world.call
Input.mx, Input.my, Input.click = 400,144,true
$game_player.update
Input.click = false
600.times do
  $game_player.update
  break if HardRPGTapWalk.driver.status == :arrived
end
raise 'tap did not reach exact tile' unless [$game_player.x,$game_player.y] == [12,4]
raise 'normal steps bypassed' unless $game_party.steps == 18
raise 'touch event bypassed' unless $game_map.events[1].starts == 1
raise 'stock encounter progression changed' unless $game_player.instance_variable_get(:@encounter_count) == 83
if kind == :xp
  raise 'stock slip hooks lost' unless $game_party.slip_checks == 9
else
  raise 'stock walk hooks lost' unless $game_party.walks == 18
end
puts "PASS: #{kind} stock player uses 18 normal steps, preserves event and encounter/slip/walk hooks"

world.call
$game_map.display_x, $game_map.display_y = 1.5*units,2.75*units
Input.mx, Input.my, Input.click = 80,48,true
$game_player.update
Input.click = false
raise 'fixed-point camera conversion failed' unless HardRPGTapWalk.driver.target == [4,4]
puts "PASS: #{kind} fractional fixed-point camera converts the tapped pixel to tile 4,4"

[2,4,6,8].each do |direction|
  world.call
  Input.mx, Input.my, Input.click = 400,144,true
  $game_player.update
  Input.click = false
  before = [$game_player.x,$game_player.y]
  Input.direction = direction
  $game_player.update
  raise 'direction failed to cancel' unless HardRPGTapWalk.driver.reason == :physical
  40.times { $game_player.update }
  after = [$game_player.x,$game_player.y]
  axis,sign = [4,6].include?(direction) ? 0 : 1, [2,6].include?(direction) ? 1 : -1
  raise 'manual movement blocked after cancellation' unless (after[axis]-before[axis])*sign > 0
  Input.direction = 0
  40.times { $game_player.update }
  raise 'old route resumed' if HardRPGTapWalk.driver.active?
end
puts "PASS: #{kind} all four directions cancel and resume stock manual movement"

world.call
# Wall target 7,4: the reachable neighbour 6,4 is the closest from this side.
Input.mx, Input.my, Input.click = 240,144,true
$game_player.update
Input.click = false
600.times do
  $game_player.update
  break if HardRPGTapWalk.driver.status == :arrived
end
raise 'wall tolerance failed' unless [$game_player.x,$game_player.y] == [6,4]
puts "PASS: #{kind} blocked tap chooses the nearest reachable tile"
