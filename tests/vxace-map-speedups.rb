# Check launcher/vxace_map_speedups.rb: when each speedup turns on, and that what it computes is what
# the game's own code computes.
src = File.read(File.expand_path('../launcher/native_events.rb', __dir__))
eval("module HardRPGNative\n#{src[/  module Norm.*?\n  end\n/m]}\nend")
module Graphics; def self.update; end; end
module Kernel; private def rgss_main; end; end
$RGSS_SCRIPTS = []
require_relative '../launcher/vxace_map_speedups'

def check(label, ok)
  raise "#{label} failed" unless ok
end

# The normalizer's quick paths (no #, no quotes) give the same lines as the full scan.
N = HardRPGNative::Norm
check('norm plain', N.norm_line("  a\t\t b  ") == 'a b')
check('norm comment', N.norm_line("  x = 1  # note") == 'x = 1')
check('norm hash in quotes', N.norm_line(%q{  s = "a#b" # c}) == 's = "a#b"' && N.norm_line(%q{t = 'x\\'#y' # z}) == %q{t = 'x\\'#y'})
check('norm interpolation', N.norm_line('  p "#{a}"  ') == 'p "#{a}"')
STOCK_VARIABLES = <<~RUBY
  class Game_Variables
    def initialize
      @data = []
    end
    def [](variable_id)
      @data[variable_id] || 0
    end
    def []=(variable_id, value)
      @data[variable_id] = value
      on_change
    end
    def on_change
      $game_map.need_refresh = true
    end
  end
RUBY
STOCK_EVENT = <<~RUBY
  class Game_Event
    def refresh
      new_page = @erased ? nil : find_proper_page
      setup_page(new_page) if !new_page || new_page != @page
    end
    def conditions_met?(page)
      c = page.condition
      if c.switch1_valid
        return false unless $game_switches[c.switch1_id]
      end
      if c.switch2_valid
        return false unless $game_switches[c.switch2_id]
      end
      if c.variable_valid
        return false if $game_variables[c.variable_id] < c.variable_value
      end
      if c.self_switch_valid
        key = [@map_id, @event.id, c.self_switch_ch]
        return false if $game_self_switches[key] != true
      end
      if c.item_valid
        item = $data_items[c.item_id]
        return false unless $game_party.has_item?(item)
      end
      if c.actor_valid
        actor = $game_actors[c.actor_id]
        return false unless $game_party.members.include?(actor)
      end
      return true
    end
  end
RUBY
# Distance switches (ろかん): its own refresh, which never reads variables.
DISTANCE = <<~RUBY
  class Game_Event < Game_Character
    alias _refresh_distance_switch refresh
    def refresh
      set_distance_switch unless @switch_distances
      _refresh_distance_switch
    end
    def set_distance_switch
      @switch_distances = @event.get_distance_switches_conf
    end
  end
RUBY
# A script whose page choice reads a variable.
READS = <<~RUBY
  class Game_Event < Game_Character
    alias _refresh_hp refresh
    def refresh
      _refresh_hp
      pick_graphic
    end
    def pick_graphic
      @character_index = $game_variables[7]
    end
  end
RUBY

def sections(*texts)
  texts.each_with_index.map { |t, i| [i, "S#{i}", '', t] }
end
F = HardRPGRefreshFilter
check('stock scripts turn the filter on', F.check(F.definitions(sections(STOCK_VARIABLES, STOCK_EVENT, DISTANCE))) == 'on')
check('a refresh that reads variables turns it off',
      F.check(F.definitions(sections(STOCK_VARIABLES, STOCK_EVENT, READS))).include?('reads variables'))
custom_set = STOCK_VARIABLES.sub('on_change', "on_change\n    @log << variable_id")
check('a changed []= turns it off', F.check(F.definitions(sections(custom_set, STOCK_EVENT))).include?('custom'))
check('no Game_Variables turns it off', F.check(F.definitions(sections(STOCK_EVENT))).include?('no Game_Variables'))

# The write rule.
Cond = Struct.new(:variable_valid, :variable_id)
Page = Struct.new(:condition)
EventData = Struct.new(:pages)
class FakeEvent; def initialize(pages); @event = EventData.new(pages); end; end
class FakeMap
  attr_accessor :need_refresh
  attr_reader :map_id
  def initialize(id, events)
    @map_id = id; @events = events
  end
end
eval(STOCK_VARIABLES)
F.instance_variable_set(:@status, nil)
$RGSS_SCRIPTS = sections(STOCK_VARIABLES, STOCK_EVENT)
class Game_Event; end
check('install', F.install == 'on')
$game_map = FakeMap.new(1, { 1 => FakeEvent.new([Page.new(Cond.new(true, 5)), Page.new(Cond.new(false, 9))]) })
v = Game_Variables.new
v[94] = 3
check('a variable no page reads does not refresh', !$game_map.need_refresh)
v[5] = 2
check('a page condition variable refreshes', $game_map.need_refresh)
$game_map.need_refresh = false
v[5] = 2
check('the same value again does not refresh', !$game_map.need_refresh)
v[5] = [1]
check('a non-integer value refreshes', $game_map.need_refresh)
$game_map = FakeMap.new(2, { 1 => FakeEvent.new([Page.new(Cond.new(true, 94))]) })
v[94] = 4
check('a new map reads its own conditions', $game_map.need_refresh)
check('values are kept', v[94] == 4 && v[5] == [1] && v[1] == 0)

# KNS animation chips: the faster update_src_rect picks the same frames.
class Rect
  attr_reader :v
  def set(*a)
    @v = a
  end
end
class Sprite_Character
  attr_reader :src_rect, :calls
  def initialize(name, character)
    @character_name = name; @character = character; @cw = 32; @ch = 48; @src_rect = Rect.new; @calls = 0
  end
  attr_writer :character_name
  def update_src_rect
    @calls += 1
  end
end
KNS_SECTION = <<~RUBY
  class Sprite_Character
    alias kns_update_src_rect update_src_rect
    def update_src_rect
      if @character_name && @character_name =~ /\\{(\\d+)\\}/
        index = @character.anime_frame %= $1.to_i
        sx, sy = (index % 6) * @cw, (index / 6) * @ch
        self.src_rect.set(sx, sy, @cw, @ch)
      else
        kns_update_src_rect
      end
    end
  end
RUBY
eval(KNS_SECTION, binding, '{0001}', 1)
$RGSS_SCRIPTS = [[0, 'Main', '', ''], [1, 'KNS', '', KNS_SECTION]]
Chara = Struct.new(:anime_frame)
runs = lambda do
  out = []
  c = Chara.new(0)
  s = Sprite_Character.new('{10}anime', c)
  14.times { |i| c.anime_frame = i; s.update_src_rect; out << s.src_rect.v.dup << c.anime_frame }
  s.character_name = 'plain'
  s.update_src_rect
  out << s.calls
  s.character_name = '{8}other'
  c.anime_frame = 9
  s.update_src_rect
  out << s.src_rect.v.dup << c.anime_frame
end
before = runs.call
check('KNS speedup installs', HardRPGAnimeChipSpeedup.install == 'on')
after = runs.call
check('KNS speedup picks the same frames', before == after)
# Unchanged switch writes.
SWITCHES = <<~RUBY
  class Game_Switches
    def initialize
      @data = []
    end
    def [](switch_id)
      @data[switch_id] || false
    end
    def []=(switch_id, value)
      @data[switch_id] = value
      on_change
    end
    def on_change
      $game_map.need_refresh = true
    end
  end
  class Game_SelfSwitches
    def initialize
      @data = {}
    end
    def [](key)
      @data[key] == true
    end
    def []=(key, value)
      @data[key] = value
      on_change
    end
    def on_change
      $game_map.need_refresh = true
    end
  end
RUBY
S = HardRPGSwitchSkip
defs = F.definitions(sections(SWITCHES))
check('stock switches turn the skip on', S.check(defs, 'Game_Switches') == 'on' && S.check(defs, 'Game_SelfSwitches') == 'on')
custom = SWITCHES.sub("@data[key] = value\n", "@data[key] = value\n    @log = 1\n")
check('a changed []= keeps it off', S.check(F.definitions(sections(custom)), 'Game_SelfSwitches') == 'off (custom)')
eval(SWITCHES)
$RGSS_SCRIPTS = sections(SWITCHES)
check('switch skip installs', S.install == 'Game_Switches on, Game_SelfSwitches on')
$game_map = FakeMap.new(1, {})
ss = Game_SelfSwitches.new
ss[[1, 2, 'A']] = false
check('nil to false refreshes', $game_map.need_refresh)
$game_map.need_refresh = false
ss[[1, 2, 'A']] = false
check('false again does not refresh', !$game_map.need_refresh)
ss[[1, 2, 'A']] = true
check('false to true refreshes', $game_map.need_refresh && ss[[1, 2, 'A']])
sw = Game_Switches.new
$game_map.need_refresh = false
sw[3] = 1
check('a non-boolean value refreshes', $game_map.need_refresh)
$game_map.need_refresh = false
sw[3] = 1
check('the same non-boolean again still refreshes', $game_map.need_refresh)

# Far off-screen sprites: which games keep it on.
O = HardRPGOffscreenSprites
native = %w[Spriteset_Map#update_characters Sprite_Character#update]
stock_sc = "class Sprite_Character\n  def update\n    super\n  end\n  def update_other\n    self.opacity = 1\n  end\nend\n"
check('stock sprites turn it on', O.check(F.definitions(sections(stock_sc, KNS_SECTION)), native) == 'on')
other = "class Sprite_Character\n  def update_other\n    $game_variables[1] = 2\n  end\nend\n"
check('a changed update_other turns it off',
      O.check(F.definitions(sections(stock_sc, other)), native) == 'off (custom Sprite_Character#update_other)')
check('a Ruby update turns it off', O.check(F.definitions(sections(stock_sc)), native.take(1)).include?('not native'))
# Kept check results: a second start reads them instead of checking the scripts again.
C = HardRPGVXAceScanCache
dir = File.expand_path("../build/tests/vxace-scan-#{$$}", __dir__)
[File.dirname(File.dirname(dir)), File.dirname(dir), dir].each { |d| Dir.mkdir(d) unless File.directory?(d) }
begin
  $hardrpg_vxace_scan_cache = File.join(dir, 'cache', 'scan.dat')
  $RGSS_SCRIPTS = sections(STOCK_VARIABLES, STOCK_EVENT)
  C.instance_variable_set(:@data, nil)
  runs = 0
  check('first start checks', C.fetch(:t) { runs += 1; 'on' } == 'on' && runs == 1)
  C.save
  check('cache written', File.file?($hardrpg_vxace_scan_cache))
  C.instance_variable_set(:@data, nil)
  check('second start reads it', C.fetch(:t) { runs += 1; 'off' } == 'on' && runs == 1)
  $RGSS_SCRIPTS = sections(STOCK_VARIABLES, STOCK_EVENT, READS)
  check('changed scripts check again', C.fetch(:t) { runs += 1; 'off' } == 'off' && runs == 2)
  check('a failing check keeps nothing', (C.fetch(:u) { raise 'x' } rescue :raised) == :raised && C.fetch(:u) { 'ok' } == 'ok')
  File.binwrite($hardrpg_vxace_scan_cache, 'garbage')
  C.instance_variable_set(:@data, nil)
  check('a damaged cache is ignored', C.fetch(:t) { 'fresh' } == 'fresh')
  # The refresh filter goes through the cache too.
  C.instance_variable_set(:@data, nil)
  $RGSS_SCRIPTS = sections(STOCK_VARIABLES, STOCK_EVENT, READS)
  F.instance_variable_set(:@status, nil)
  check('refresh filter, reads variables', F.install.include?('reads variables'))
  C.save
  C.instance_variable_set(:@data, nil)
  F.instance_variable_set(:@status, nil)
  check('refresh filter, kept result', F.install.include?('reads variables'))
ensure
  $hardrpg_vxace_scan_cache = nil
  Dir.glob(File.join(dir, '**', '*')).sort.reverse.each { |f| File.directory?(f) ? Dir.rmdir(f) : File.delete(f) }
  Dir.rmdir(dir)
end
# The database loads with collection paused, and collection is back on afterwards (also after an error).
module DataManager
  def self.load_database; $gc_during = GC.disable; GC.enable unless $gc_during; :loaded; end
end
HardRPGDatabaseLoad.wrap
check('database load', DataManager.load_database == :loaded && $gc_during == true && GC.enable == false)
module DataManager
  class << self
    alias_method :plain_load, :load_database
  end
end
check('wrapped once', (HardRPGDatabaseLoad.wrap; true))
puts 'VX Ace map speedup checks passed'
