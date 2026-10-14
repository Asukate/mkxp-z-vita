# Exercise the adapter with user-provided stock VX Ace character/player scripts.
# Original scripts stay outside the repository; pass their extraction directory.
require 'json'
root = ARGV.fetch(0)
module Graphics
  def self.width; 544; end
  def self.height; 416; end
end
module Input
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
%w[029.rb 030.rb 031.rb].each { |name| load File.join(root, name) }
require_relative 'fixtures/tap_walk_world'
require_relative '../launcher/vita_tap_to_walk'
data = {'width' => 20, 'height' => 16, 'walls' => (1..8).map { |y| [7, y] }}
setup_walk_world(data)
HardRPGTapWalk.install!
Input.mx, Input.my, Input.click = 12 * 32 + 16, 4 * 32 + 16, true
$game_player.update
Input.click = false
500.times do
  $game_player.update
  break if HardRPGTapWalk.driver.status == :arrived
end
raise 'tap did not reach target' unless [$game_player.x, $game_player.y] == [12, 4]
raise 'ordinary step hooks bypassed' unless $game_party.steps == 18 && $game_party.walks == 18
raise 'follower movement bypassed' unless $game_player.followers.moves == 18
raise 'touch event lost' unless $game_map.events[1].starts == 1
raise 'encounter progression lost' unless $game_player.instance_variable_get(:@encounter_count) == 82
puts 'PASS: stock VX Ace player reached tapped tile around wall in 18 ordinary steps'
puts 'PASS: followers, party walk hooks, touch event and encounter countdown preserved'

# Native-facing mouse coordinates -> the same adapter -> stock move_by_input.
Input.mx, Input.my, Input.click = 2 * 32 + 16, 4 * 32 + 16, true
$game_player.update
Input.click = false
100.times do
  $game_player.update
  break if $game_player.x < 12
end
Input.direction = 2
$game_player.update
raise 'manual direction did not cancel route' if HardRPGTapWalk.driver.active?
30.times { $game_player.update }
raise 'stock manual input not restored' unless $game_player.y > 4
puts 'PASS: Down immediately cancelled the route and resumed stock manual movement'
puts JSON.generate(:target => [12,4], :steps => 18, :touch_events => 1,
                   :encounters_after_route => 82, :manual_cancel => true)

[2,4,6,8].each do |direction|
  setup_walk_world(data)
  HardRPGTapWalk.install!
  Input.direction = 0
  Input.mx, Input.my, Input.click = 400,144,true
  $game_player.update
  Input.click = false
  20.times do
    $game_player.update
    break if $game_player.moving?
  end
  before = [$game_player.x,$game_player.y]
  Input.direction = direction
  $game_player.update
  raise 'manual cancellation delayed' unless HardRPGTapWalk.driver.reason == :physical
  40.times { $game_player.update }
  after = [$game_player.x,$game_player.y]
  axis = [4,6].include?(direction) ? 0 : 1
  sign = [2,6].include?(direction) ? 1 : -1
  raise 'stock manual direction lost' unless (after[axis]-before[axis])*sign > 0
  Input.direction = 0
  40.times { $game_player.update }
  raise 'cancelled route resumed' if HardRPGTapWalk.driver.active?
end
puts 'PASS: all four directions cancel during movement and retain manual control'
