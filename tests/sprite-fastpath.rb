# Host checks for launcher/sprite_fastpath.rb. The real checksums are for the engine's Ruby 3.1 and the
# default RPG Maker scripts, which are not in this repository; here small stand-in classes play the
# default scripts and the checksums are taken from them with this Ruby. What is checked:
# - the checksum ignores line numbers and the name invokesuper records after running;
# - an edited method, an inherited one, or $hardrpg_sprite_fastpath = false keeps the game's update;
# - the shorter updates leave every sprite exactly as the full update does (XP), or differ only in the
#   position of sprites that draw nothing (VX Ace).
require 'zlib'

def check(label, ok)
  raise "FAIL #{label}" unless ok
  puts "ok #{label}"
end

def load_fastpath
  Object.send(:remove_const, :HardRPGSpriteFastPath) if defined?(HardRPGSpriteFastPath)
  load File.expand_path('../launcher/sprite_fastpath.rb', __dir__)
end

class Rect; attr_reader :v; def set(*a); @v = a; end; end
class Sprite
  attr_accessor :x, :y, :z, :opacity, :blend_type, :bush_depth, :visible, :bitmap, :ox, :oy
  def initialize(_viewport = nil); @x = @y = @z = 0; end
  def update; @base_updates = (@base_updates || 0) + 1; end
  def src_rect; @r ||= Rect.new; end
end
module Graphics; def self.update; end; end
load_fastpath

# --- checksum basics -------------------------------------------------------------------------------
a = Class.new(Sprite) { eval("def f(x)\n  super\n  x + 1\nend") }
b = Class.new(Sprite) { eval("\n\n\ndef f(x)\n  super\n  x + 1\nend") }
c = Class.new(Sprite) { eval("def f(x)\n  super\n  x + 2\nend") }
da = HardRPGSpriteFastPath.digest(a.instance_method(:f))
check('checksum ignores line numbers', da == HardRPGSpriteFastPath.digest(b.instance_method(:f)))
check('checksum sees an edited method', da != HardRPGSpriteFastPath.digest(c.instance_method(:f)))
Sprite.class_eval { def f(x); x; end }
a.new.f(1)
check('checksum unchanged after super has run', da == HardRPGSpriteFastPath.digest(a.instance_method(:f)))
check('inherited method is not the class\'s own', !HardRPGSpriteFastPath.default?(Class.new(a), :f, da))

# --- RPG Maker XP stand-ins ------------------------------------------------------------------------
module RPG; class Sprite < ::Sprite; end; end
class Game_Map; attr_accessor :display_x, :display_y; def bush?(x, _y); x == 3; end; end
class Game_Character
  attr_accessor :x, :y, :real_x, :real_y, :pattern, :direction, :opacity, :blend_type, :transparent,
                :tile_id, :character_name, :character_hue, :animation_id, :always_on_top, :jump_count
  def initialize(x)
    @x = x; @y = 2; @real_x = x * 128; @real_y = 256; @pattern = 0; @direction = 2; @opacity = 255
    @blend_type = 0; @transparent = false; @tile_id = 0; @character_name = 'A'; @character_hue = 0
    @animation_id = 0; @always_on_top = false; @jump_count = 0
  end
  def screen_x; (@real_x - $game_map.display_x + 3) / 4 + 16; end
  def screen_y; (@real_y - $game_map.display_y + 3) / 4 + 32 - @jump_count; end
  def screen_z(_h = 0); @always_on_top ? 999 : (@real_y - $game_map.display_y + 3) / 4 + 32; end
  def bush_depth; @always_on_top || @jump_count > 0 || !$game_map.bush?(@x, @y) ? 0 : 12; end
  def jumping?; @jump_count > 0; end
end
class Sprite_Character < RPG::Sprite
  def initialize(viewport, character); super(viewport); @character = character; update; end
  def update
    super
    if @tile_id != @character.tile_id || @character_name != @character.character_name ||
       @character_hue != @character.character_hue
      @tile_id = @character.tile_id; @character_name = @character.character_name; @character_hue = @character.character_hue
      @cw = 32; @ch = 48
    end
    self.visible = !@character.transparent
    src_rect.set(@character.pattern * @cw, (@character.direction - 2) / 2 * @ch, @cw, @ch) if @tile_id == 0
    self.x = @character.screen_x
    self.y = @character.screen_y
    self.z = @character.screen_z(@ch)
    self.opacity = @character.opacity
    self.blend_type = @character.blend_type
    self.bush_depth = @character.bush_depth
  end
end

def xp_digests
  fp = HardRPGSpriteFastPath
  fp.send(:remove_const, :XP_SPRITE_UPDATE); fp.send(:remove_const, :XP_CHARACTER)
  fp.const_set(:XP_SPRITE_UPDATE, fp.digest(Sprite_Character.instance_method(:update)))
  fp.const_set(:XP_CHARACTER, %i[screen_x screen_y screen_z bush_depth jumping?].to_h { |m| [m, fp.digest(Game_Character.instance_method(m))] })
end

def xp_run
  $game_map = Game_Map.new; $game_map.display_x = 0; $game_map.display_y = 0
  cs = (2..4).map { |i| Game_Character.new(i) }
  sprites = cs.map { |ch| Sprite_Character.new(nil, ch) }
  20.times.map do |f|
    cs[0].real_x += 32 if f < 4
    cs[0].x = cs[0].real_x / 128
    cs[1].pattern = f % 4 if f.between?(5, 7)
    $game_map.display_x += 64 if f == 9
    cs[2].opacity = 128 if f == 10
    cs[2].jump_count = 3 if f == 12
    cs[2].jump_count = 0 if f == 14
    cs[1].always_on_top = true if f == 16
    cs[0].character_name = 'B' if f == 18
    sprites.each(&:update)
    sprites.map { |s| [s.x, s.y, s.z, s.opacity, s.blend_type, s.bush_depth, s.visible, s.src_rect.v] }
  end
end

load_fastpath
xp_digests
$hardrpg_sprite_fastpath = false
HardRPGSpriteFastPath.install
check('XP: switched off by $hardrpg_sprite_fastpath', HardRPGSpriteFastPath.status == 'off (disabled)')
$hardrpg_sprite_fastpath = nil

reference = xp_run
load_fastpath
xp_digests
HardRPGSpriteFastPath.install
check('XP: installs on the default scripts', HardRPGSpriteFastPath.status == 'on (XP)')
check('XP: every sprite matches the full update in every frame', xp_run == reference)

# A game that edited one Game_Character method keeps the full update.
Object.send(:remove_const, :Sprite_Character)
class Sprite_Character < RPG::Sprite; end
Sprite_Character.class_eval { def update; super; end }
load_fastpath
xp_digests
HardRPGSpriteFastPath.send(:remove_const, :XP_CHARACTER)
HardRPGSpriteFastPath.const_set(:XP_CHARACTER, { screen_x: '00000000' })
HardRPGSpriteFastPath.install
check('XP: an edited method keeps the game\'s update', HardRPGSpriteFastPath.status.start_with?('off (custom Game_Character'))
# --- RPG Maker VX Ace stand-ins --------------------------------------------------------------------
# A fresh set of classes in a module, so the XP ones above do not interfere.
module VXA
  class Sprite_Base < ::Sprite
    def update; super; end
    def animation?; !@animation.nil?; end
    def start_animation(a); @animation = a; end
  end
  class Character
    attr_accessor :tile_id, :character_name, :character_index, :pattern, :direction, :screen_x, :screen_y,
                  :screen_z, :opacity, :blend_type, :bush_depth, :transparent, :animation_id, :balloon_id
    def initialize(name, x)
      @tile_id = 0; @character_name = name; @character_index = 0; @pattern = 1; @direction = 2
      @screen_x = x; @screen_y = 20; @screen_z = 1; @opacity = 255; @blend_type = 0; @bush_depth = 0
      @transparent = false; @animation_id = 0; @balloon_id = 0
    end
  end
  SPRITE = <<~RUBY
    def initialize(viewport, character); super(viewport); @character = character; @balloon_duration = 0; update; end
    def update; super; update_bitmap; update_src_rect; update_position; update_other; update_balloon; setup_new_effect; end
    def update_bitmap
      if graphic_changed?
        @tile_id = @character.tile_id; @character_name = @character.character_name; @character_index = @character.character_index
        @cw = 32; @ch = 32
      end
    end
    def graphic_changed?
      @tile_id != @character.tile_id || @character_name != @character.character_name || @character_index != @character.character_index
    end
    def update_src_rect; src_rect.set(@character.pattern * @cw, @character.direction * @ch, @cw, @ch) if @tile_id == 0; end
    def update_position
      move_animation(@character.screen_x - x, @character.screen_y - y)
      self.x = @character.screen_x; self.y = @character.screen_y; self.z = @character.screen_z
    end
    def update_other
      self.opacity = @character.opacity; self.blend_type = @character.blend_type
      self.bush_depth = @character.bush_depth; self.visible = !@character.transparent
    end
    def update_balloon; end
    def setup_new_effect; start_animation(:anim) if !animation? && @character.animation_id > 0; end
    def move_animation(dx, dy); @moved = (@moved || 0) + dx + dy if @animation; end
  RUBY
end

def vxa_classes(variant)
  Object.send(:remove_const, :Sprite_Base) if defined?(::Sprite_Base)
  Object.send(:remove_const, :Sprite_Character) if defined?(::Sprite_Character)
  Object.const_set(:Sprite_Base, VXA::Sprite_Base)
  sc = Class.new(Sprite_Base)
  Object.const_set(:Sprite_Character, sc)
  sc.class_eval(VXA::SPRITE)
  case variant
  when :sp1 # original position method kept under an alias, wrapped like VXAce_SP1 does
    sc.class_eval { alias_method :vxace_sp1_update_position, :update_position }
    sc.class_eval("def update_position\n move_animation(@character.screen_x - x, @character.screen_y - y)\n vxace_sp1_update_position\nend")
  when :mog # default update kept under an alias, wrapped like MOG Anti Lag does
    sc.class_eval { alias_method :mog_anti_lag_update, :update }
    sc.class_eval("def update\n return if @character.transparent\n mog_anti_lag_update\nend")
  end
  sc
end

def vxa_digests(variant)
  fp = HardRPGSpriteFastPath
  ref = vxa_classes(:plain)
  vxa = HardRPGSpriteFastPath::VXA.keys.to_h { |m| [m, fp.digest(ref.instance_method(m))] }
  fp.send(:remove_const, :VXA); fp.const_set(:VXA, vxa)
  live = vxa_classes(variant)
  if variant == :sp1
    fp.send(:remove_const, :VXA_SP1_POSITION); fp.const_set(:VXA_SP1_POSITION, fp.digest(live.instance_method(:update_position)))
    fp.send(:remove_const, :VXA_JP_POSITION); fp.const_set(:VXA_JP_POSITION, fp.digest(live.instance_method(:vxace_sp1_update_position)))
  elsif variant == :mog
    fp.send(:remove_const, :VXA_MOG_UPDATE); fp.const_set(:VXA_MOG_UPDATE, fp.digest(live.instance_method(:update)))
  end
end

def vxa_run
  cs = [VXA::Character.new('Actor1', 100), VXA::Character.new('', 10)]
  sprites = cs.map { |ch| Sprite_Character.new(nil, ch) }
  12.times.map do |f|
    cs[0].screen_x += 3; cs[0].pattern = f % 3; cs[1].screen_x += 1
    cs[1].animation_id = 1 if f == 6      # an animation starts on the invisible event
    cs[1].character_name = 'Evil' if f == 9 # and later it gets a graphic
    sprites.each(&:update)
    sprites.map { |s| [s.x, s.y, s.z, s.opacity, s.blend_type, s.visible, s.src_rect.v, s.animation?] }
  end
end

vxa_classes(:plain)
reference = vxa_run
load_fastpath
vxa_digests(:plain)
HardRPGSpriteFastPath.install
check('VX Ace: installs on the default scripts', HardRPGSpriteFastPath.status == 'on (VX Ace)')
fast = vxa_run
check('VX Ace: sprites with a graphic match the full update in every frame', fast.map { |r| r[0] } == reference.map { |r| r[0] })
check('VX Ace: the graphic-less sprite keeps its old position until something shows', fast[0..5].all? { |r| r[1][0] == 10 })
check('VX Ace: it matches again once an animation or graphic appears', fast[6..].map { |r| r[1] } == reference[6..].map { |r| r[1] })
%i[sp1 mog].each do |variant|
  load_fastpath
  vxa_digests(variant)
  HardRPGSpriteFastPath.install
  want = variant == :mog ? 'on (VX Ace, MOG Anti Lag)' : 'on (VX Ace)'
  check("VX Ace: installs with #{variant == :mog ? 'MOG Anti Lag' : 'VXAce_SP1'}", HardRPGSpriteFastPath.status == want)
end
load_fastpath
vxa_digests(:plain)
Sprite_Character.class_eval { def update_other; self.opacity = 1; end }
HardRPGSpriteFastPath.install
check('VX Ace: an edited update_other keeps the game\'s update', HardRPGSpriteFastPath.status == 'off (custom update_other)')
puts 'sprite-fastpath: all checks passed'
