# Check HardRPGOffscreenSprites' Ruby parking for Galv's Character Animations: a sprite far off the
# screen skips the default update but still runs Galv's step; one near the screen, or with an
# animation or balloon, runs its whole update.
src = File.read(File.expand_path('../launcher/native_events.rb', __dir__), encoding: 'UTF-8')
eval("module HardRPGNative\n#{src[/  module Norm.*?\n  end\n/m]}\nend")
module Graphics; def self.update; end; def self.width = 544; def self.height = 416; end
module Kernel; private def rgss_main; end; end
$RGSS_SCRIPTS = []
require_relative '../launcher/vxace_map_speedups'

def check(label, ok)
  raise "#{label} failed" unless ok
end

module Chara_Anims; ANIM_SWITCH = 66; COMMON_EVENT_TIME = 300; end
Chr = Struct.new(:screen_x, :screen_y, :animation_id, :balloon_id)
class Sprite_Base; attr_accessor :x, :y; end
class Sprite_Character < Sprite_Base
  attr_reader :inner, :idle, :moves, :idles
  def initialize(c, x, y) = (@character, @x, @y, @inner, @idletime, @moves, @idles = c, x, y, 0, 0, 0, 0)
  def galv_charanim_update = @inner += 1
  def update
    galv_charanim_update
    return if $game_switches[Chara_Anims::ANIM_SWITCH]
    return move_anim if $game_player.moving?
    @idletime += 1
    idle_anim if @idletime == 5
    idle_event if @idletime == Chara_Anims::COMMON_EVENT_TIME
  end
  def move_anim = (@moves += 1; @idletime = 0)
  def idle_anim = (@idles += 1; @idletime += 1)
  def idle_event; end
  def idletime = @idletime
end
Sprite_Character.prepend(HardRPGOffscreenSprites::GalvPark)
Player = Struct.new(:moving) { def moving? = moving }
$game_switches = {}
$game_player = Player.new(false)

far = Sprite_Character.new(Chr.new(3000, 100, 0, 0), 3000, 100)
near = Sprite_Character.new(Chr.new(600, 100, 0, 0), 600, 100)
was_near = Sprite_Character.new(Chr.new(3000, 100, 0, 0), 200, 100) # drawn on screen last time
anim = Sprite_Character.new(Chr.new(3000, 100, 4, 0), 3000, 100)
all = [far, near, was_near, anim]
10.times { all.each(&:update) }
check('far parked', far.inner == 0 && far.idletime == 11 && far.idles == 1)
check('near updates', near.inner == 10 && near.idletime == 11 && near.idles == 1)
check('last drawn near updates', was_near.inner == 10)
check('animation updates', anim.inner == 10)
$game_player.moving = true
far.update
check('parked still runs the move step', far.moves == 1 && far.idletime == 0 && far.inner == 0)
$game_switches[66] = true
far.update
check('switch stops the step', far.moves == 1)
puts 'Galv parking checks passed'
