# Check launcher/legacy_ruby_compat.rb against the parser it runs behind.
# Each case is shipped-game syntax that Ruby 1.8/1.9 accepted; the test
# raises the real SyntaxError, repairs with the real message, the way the
# engine does, and checks what the repaired section computes.
require_relative '../launcher/legacy_ruby_compat'

# WF-RGSS version probes (Shin Megami Tensei VX). This host has no rgss_main
# and no RPG::Cache, so it answers as a VX game would.
module Kernel
  private
  def rpgvx?
    defined? Graphics.resize_screen
  end
  def rpgvxace?
    defined? BasicObject
  end
  def wfrgss_other_probe?
    :untouched
  end
end
raise 'rpgvxace? not answered from the engine' unless rpgvxace? == false
raise 'rpgvx? not answered from the engine' unless rpgvx? == true
raise 'probe visibility changed' unless Kernel.private_method_defined?(:rpgvxace?)
raise 'other Kernel methods touched' unless wfrgss_other_probe? == :untouched

# Peephole guard: only WF-RGSS games compile without it.
raise 'peephole turned off without WF-RGSS' if RubyVM::InstructionSequence.compile_option[:peephole_optimization] == false
raise 'WF-RGSS section not recognised' unless HardRPGCompileGuard.wf_rgss?([[1, 'WF-RGSS Scripts Base rev-29', '', '']])
raise 'WF-RGSS text not recognised' unless HardRPGCompileGuard.wf_rgss?([[1, 'Base', '', '# WF-RGSS base']])
raise 'other game taken for WF-RGSS' if HardRPGCompileGuard.wf_rgss?([[1, 'Main', '', 'rgss_main { }']]) || HardRPGCompileGuard.wf_rgss?(nil)

# Saba 3D Dungeon redraws: held until the frame is shown, deduplicated.
raise 'dungeon script not recognised' unless HardRPGDungeonRedraw.uses_script?([[1, '3D', '', "class Dungeon_Sprite\n  def refresh_3d\n"]])
raise 'other game taken for the dungeon script' if HardRPGDungeonRedraw.uses_script?([[1, 'Main', '', 'rgss_main { }']])
TestPage = Object.new
class TestEvent
  attr_accessor :id, :x, :y, :direction, :through, :transparent, :tile_id, :character_name, :character_index
  def initialize(x, y)
    @id = 1; @x = x; @y = y; @direction = 2; @through = false; @transparent = false
    @tile_id = 0; @character_name = 'door'; @character_index = 0; @page = TestPage
  end
end
class TestMap
  attr_reader :events, :data, :map_id
  def initialize; @events = { 1 => TestEvent.new(3, 3) }; @data = []; @map_id = 7; @animation_type = 0; end
end
class Game_Player
  attr_reader :x, :y, :direction
  def initialize; @x = 1; @y = 1; @direction = 8; @move_front = false; @rotation_left = false; @rotation_right = false; end
  def move_front?; @move_front; end
  def rotation_left?; @rotation_left; end
  def rotation_right?; @rotation_right; end
end
class TestBitmap; def disposed?; false; end; end
class Dungeon_Sprite
  attr_reader :draws
  def initialize; @sprites = []; @bitmaps = [TestBitmap.new]; @dark = Object.new; @character_sprites = []; @draws = []; end
  def refresh_3d; @draws << [$game_player.x, $game_player.move_front?, $game_map.instance_variable_get(:@animation_type)]; end
end
$game_player = Game_Player.new
$game_map = TestMap.new
$game_switches = []
raise 'dungeon patch not installed' unless HardRPGDungeonRedraw.install
view = Dungeon_Sprite.new
view.refresh_3d
raise 'drawn before the frame was shown' unless view.draws.empty?
HardRPGDungeonRedraw.flush
raise 'first view not drawn' unless view.draws == [[1, false, 0]]
# Held into a wall: half-step view, then the same view as before, each frame.
3.times do
  $game_player.instance_variable_set(:@move_front, true); $game_map.instance_variable_set(:@animation_type, 2)
  view.refresh_3d
  $game_player.instance_variable_set(:@move_front, false)
  view.refresh_3d
  HardRPGDungeonRedraw.flush
end
raise "wall bump frames redrew: #{view.draws.inspect}" unless view.draws == [[1, false, 0]]
# A real step: half-step view requested, then the move lands in the same frame.
$game_player.instance_variable_set(:@move_front, true)
view.refresh_3d
$game_player.instance_variable_set(:@x, 2)
HardRPGDungeonRedraw.flush
raise "half step not drawn where it was asked: #{view.draws.inspect}" unless view.draws.last == [1, true, 2]
raise 'player not restored after the late draw' unless $game_player.x == 2 && $game_player.move_front?
# A door opening (event page change) must redraw the same position.
$game_player.instance_variable_set(:@move_front, false); $game_map.instance_variable_set(:@animation_type, 0)
view.refresh_3d; HardRPGDungeonRedraw.flush
count = view.draws.size
$game_map.events[1].instance_variable_set(:@page, Object.new)
view.refresh_3d; HardRPGDungeonRedraw.flush
raise 'event change did not redraw' unless view.draws.size == count + 1
view.refresh_3d; HardRPGDungeonRedraw.flush
raise 'unchanged view redrawn' unless view.draws.size == count + 1

# KGC Passive Skills: the actor copy is skipped only when nothing reads it.
kgc = "def restore_passive_rev\n  dummy = Marshal.load(Marshal.dump(self)) unless @passive_dummy_flag\nend\nattr_accessor :passive_dummy_flag\n"
raise 'passive script not recognised' unless HardRPGPassiveSkillFast.uses_script?([[1, 'KGC', '', kgc]])
raise 'other game taken for the passive script' if HardRPGPassiveSkillFast.uses_script?([[1, 'Main', '', 'rgss_main { }']])
module RPG
  class Skill
    attr_reader :passive, :passive_conditions, :passive_effects
    def initialize(passive, conditions = {}, effects = {})
      @passive, @passive_conditions, @passive_effects = passive, conditions, effects
    end
  end
end
$passive_copies = 0
class Game_Actor
  attr_accessor :passive_dummy_flag, :skills, :weapons, :bonus
  def initialize(skills, weapons = []); @skills = skills; @weapons = weapons; end
  def restore_passive_rev
    unless @passive_dummy_flag
      dummy = Marshal.load(Marshal.dump(self))
      $passive_copies += 1
      dummy.passive_dummy_flag = true
      dummy.restore_passive_rev
    end
    @bonus = @skills.count { |s| passive_skill_valid?(s, dummy) }
  end
  def passive_skill_valid?(skill, dummy)
    return false unless skill.passive
    return true if @passive_dummy_flag
    skill.passive_conditions.each { |_k, v| next if v.empty?; return false if (v & dummy.weapons).empty? }
    true
  end
end
raise 'passive patch not installed' unless HardRPGPassiveSkillFast.install
check_passive = lambda { |label, ok| raise label unless ok }
plain = Game_Actor.new([RPG::Skill.new(true), RPG::Skill.new(false), RPG::Skill.new(true, weapon: [])])
plain.restore_passive_rev
check_passive.('copy made without conditions', $passive_copies == 0)
check_passive.('bonus without conditions', plain.bonus == 2)
check_passive.('dummy flag left behind', !plain.instance_variable_defined?(:@passive_dummy_flag))
armed = Game_Actor.new([RPG::Skill.new(true, weapon: [5])], [5])
armed.restore_passive_rev
check_passive.('copy skipped with a condition', $passive_copies == 1 && armed.bonus == 1)
unarmed = Game_Actor.new([RPG::Skill.new(true, weapon: [5])], [3])
unarmed.restore_passive_rev
check_passive.('condition ignored', $passive_copies == 2 && unarmed.bonus == 0)
dual = Game_Actor.new([RPG::Skill.new(true, {}, two_swords_style: true)])
dual.restore_passive_rev
check_passive.('copy skipped for two-weapon skill', $passive_copies == 3 && dual.bonus == 1)

# WF-RGSS Thread.critical blocks.
saved = Thread.critical
Thread.critical = true
raise 'Thread.critical not kept' unless Thread.critical == true
Thread.critical = saved
raise 'Thread.critical not restored' unless Thread.critical == false

# Ruby 1.8 Object#to_a (Shin Megami Tensei VX state ids).
raise 'Integer#to_a missing' unless 77.to_a == [77] && 77.to_a[0] == 77
raise 'Range#to_a changed' unless (1..3).to_a == [1, 2, 3]
raise 'nil.to_a changed' unless nil.to_a == []
raise 'Hash#to_a changed' unless({1 => 2}.to_a == [[1, 2]])
raise 'Array() changed' unless Array(5) == [5] && Array(nil) == [] && [*7] == [7]

# WF-RGSS clears $! after each scene (Ruby 1.8 allowed it).
wf = "def main\n  begin\n    run\n    $! = nil\n  rescue Exception\n    raise\n  end\nend\n$! = nil unless x\n"
fixed = HardRPGLegacyErrinfo.rewrite(wf)
raise 'errinfo clear not rewritten' if fixed.nil? || fixed.include?('$! =')
raise 'errinfo rewrite changed line count' unless fixed.count("\n") == wf.count("\n")
raise 'errinfo comparison touched' unless HardRPGLegacyErrinfo.rewrite('x if $! == nil').nil?
raise 'section without $! touched' unless HardRPGLegacyErrinfo.rewrite('a = nil').nil?
sections = [[1, 'WF', '', wf.dup], [2, 'Plain', '', 'p 1']]
HardRPGLegacyErrinfo.apply(sections)
raise 'apply did not rewrite' unless sections[0][3] == fixed && sections[1][3] == 'p 1'
RubyVM::InstructionSequence.compile(fixed)

if RUBY_VERSION >= '3.4'
  puts "skipped: Ruby #{RUBY_VERSION} parses with Prism; run under Ruby 3.1 (the engine's) to 3.3"
  exit
end

def check(label, expected, actual)
  raise "#{label}: expected #{expected.inspect}, got #{actual.inspect}" unless expected == actual
end

def repair_and_eval(label, source, binding_ = binding)
  10.times do
    begin
      return eval(source, binding_, label)
    rescue SyntaxError => error
      repaired = HardRPGLegacySyntax.repair(source, error.message)
      raise "#{label}: no repair for #{error.message}" unless repaired
      check("#{label} line count", source.count("\n"), repaired.count("\n"))
      source = repaired
    end
  end
  raise "#{label}: did not converge"
end

# PALETTA: MapNode parent lookup.
check('Hash#index', 6, {4 => :a, 6 => :b}.index(:b))
check('Hash#index missing', nil, {4 => :a}.index(:c))

# The Endless Empty {0134}: begin/else with no rescue.
log = []
result = repair_and_eval('{0134}', <<~RUBY, binding)
  def endless_load(log)
    begin
      log << :body
    else
      log << :else
    end
    :done
  end
  endless_load(log)
RUBY
check('else result', :done, result)
check('else order', [:body, :else], log)
raised = begin
  repair_and_eval('{0135}', "begin\n  raise ArgumentError, 'x'\nelse\n  :else\nend\n")
rescue ArgumentError => error
  error.message
end
check('else re-raises', 'x', raised)

# VorlorN Section104: space between method name and argument list.
Color = Struct.new(:r, :g, :b) unless defined?(Color)
colors = repair_and_eval('Section104', <<~RUBY)
  a = Color.new (189, 225, 242)
  b = Color.new (5, 88, 168)
  c = [Color.new (1, 2, 3), (4)]
  [a, b, c]
RUBY
check('spaced call', [Color.new(189, 225, 242), Color.new(5, 88, 168),
                      [Color.new(1, 2, 3), 4]], colors)
check('grouping untouched', 'x = foo (a), b', HardRPGLegacySyntax.join_spaced_call('x = foo (a), b'))
check('quoted paren', 'foo("(", 2)', HardRPGLegacySyntax.join_spaced_call('foo ("(", 2)'))
check('keyword untouched', 'if (a, b)', HardRPGLegacySyntax.join_spaced_call('if (a, b)'))

# Ruby 1.8 `when x:` clauses.
kinds = repair_and_eval('Section010', <<~RUBY)
  [1, 2, 3].map do |n|
    case n
    when 1: :one
    when 2 : :two
    else :many
    end
  end
RUBY
check('when colon', [:one, :two, :many], kinds)

# A section that fails for another reason gets no rewrite.
check('unrelated error', nil, HardRPGLegacySyntax.repair("x = (\n", "{0001}:1: syntax error, unexpected end-of-input"))
puts 'legacy Ruby compatibility checks passed'
