# Check launcher/cheats.rb: the Cheats page protocol and every cheat, against
# small stand-ins for the XP, VX Ace and Essentials game objects (the same
# methods and fields the stock scripts have). Each engine runs in its own
# Ruby process, since the script tells them apart by the classes defined.
#   ruby tests/cheats.rb
require 'json'

ENGINE = ARGV[0]
unless ENGINE
  ok = %w[xp ace pe].all? { |e| system(RbConfig.ruby, __FILE__, e) }
  puts(ok ? 'cheats: all engines OK' : 'cheats: FAILED')
  exit(ok ? 0 : 1)
end

def check(what)
  raise "#{ENGINE}: #{what}" unless yield
end

Item = Struct.new(:id, :name)
DataSystem = Struct.new(:switches, :variables, :currency_unit)
MapInfo = Struct.new(:name)

if ENGINE == 'ace'
  RGSS_VERSION = '3.0.1'
  module RPG; class BaseItem; end; end
end

class Game_Battler
  attr_reader :hp, :states
  def initialize
    @hp = 10
    @states = []
  end
  def hp=(v)
    @hp = [[v, max].min, 0].max
  end
  def max
    ENGINE == 'ace' ? mhp : maxhp
  end
  def remove_state(id)
    @states.delete_if { |s| (s.respond_to?(:id) ? s.id : s) == id }
  end
  def recover_all
    @hp = max
    @states.clear
  end
end

class Game_Actor < Game_Battler
  attr_reader :id, :name, :level, :exp
  def initialize(id, name)
    super()
    @id = id
    @name = name
    @level = 1
    @exp = 0
    @mp = 0
    @param_plus = [0] * 8
    @str = 5
  end
  def actor?; true; end
  if ENGINE == 'ace'
    def mhp; 100; end
    def mmp; 50; end
    def mp; @mp; end
    def mp=(v); @mp = [[v, mmp].min, 0].max; end
    def max_level; 99; end
    def change_level(l, _show); @level = l; @exp = l * 10; end
    def change_exp(e, _show); @exp = e; @level = [e / 10, 1].max; end
    def gain_exp(e); change_exp(@exp + e, true); end
    def param(i); 10 + @param_plus[i]; end
    def add_param(i, v); @param_plus[i] += v; end
    def death_state_id; 1; end
  else
    def maxhp; 100; end
    def maxsp; 50; end
    def sp; @mp; end
    def sp=(v); @mp = [[v, maxsp].min, 0].max; end
    def level=(l); @level = l; self.exp = l * 10; end
    def exp=(e); @exp = e; @level = [e / 10, 1].max; end
    attr_accessor :str
    def dex; 1; end
    def agi; 1; end
    def int; 1; end
    def maxsp=(_v); end
    def maxhp=(_v); end
    def dex=(_v); end
    def agi=(_v); end
    def int=(_v); end
  end
end

class Game_Enemy < Game_Battler
  def actor?; false; end
  def maxhp; 500; end
  def mhp; 500; end
  def exist?; true; end
end

class Game_Party
  attr_reader :gold
  def initialize
    @gold = 0
    @items = Hash.new(0)
  end
  def gain_gold(n); @gold = [[@gold + n, 0].max, respond_to?(:max_gold) ? max_gold : 9_999_999].min; end
  def lose_gold(n); gain_gold(-n); end
  if ENGINE == 'ace'
    def all_members; $actors; end
    def battle_members; $actors; end
    def in_battle; $in_battle; end
    def item_number(i); @items[i]; end
    def gain_item(i, n); @items[i] = [[@items[i] + n, 0].max, 99].min; end
    def lose_item(i, n); gain_item(i, -n); end
    def consume_item(i); lose_item(i, 1); end
    def max_gold; 99_999_999; end
  else
    def actors; $actors; end
    def item_number(id); @items[[:i, id]]; end
    def weapon_number(id); @items[[:w, id]]; end
    def armor_number(id); @items[[:a, id]]; end
    def gain_item(id, n); @items[[:i, id]] = [[@items[[:i, id]] + n, 0].max, 99].min; end
    def gain_weapon(id, n); @items[[:w, id]] = [[@items[[:w, id]] + n, 0].max, 99].min; end
    def gain_armor(id, n); @items[[:a, id]] = [[@items[[:a, id]] + n, 0].max, 99].min; end
  end
end

class Game_Player
  attr_reader :x, :y, :direction, :move_speed
  attr_accessor :reserved
  def initialize
    @x = 3
    @y = 4
    @direction = 2
    @move_speed = 4
    @encounter_count = 20
  end
  def encounter_count; @encounter_count; end
  def update
    @encounter_count -= 1
  end
  if ENGINE == 'ace'
    def debug_through?; false; end
    def passable?(_x, _y, _d); debug_through? || false; end
    def reserve_transfer(*a); @reserved = a; end
  else
    def passable?(_x, _y, _d, *_rest); false; end
  end
end

class Game_Map
  attr_accessor :need_refresh
  def map_id; 1; end
  def valid?(x, y); x >= 0 && y >= 0 && x < 20 && y < 15; end
  def width; 20; end
  def height; 15; end
end

class Game_System
  attr_accessor :save_disabled, :menu_disabled
end

class Game_Temp
  attr_accessor :in_battle, :player_transferring, :player_new_map_id, :player_new_x, :player_new_y,
                :player_new_direction, :battle_abort
end

class Game_Troop
  def initialize; @e = [Game_Enemy.new, Game_Enemy.new]; @e.each { |e| e.hp = 500 }; end
  def members; @e; end
  def enemies; @e; end
end

class Scene_Map; end
class Scene_Battle; end
class Scene_Save; end
module SceneManager
  def self.scene; $scene; end
  def self.call(k); $called = k; end
end if ENGINE == 'ace'
module BattleManager
  def self.abort; $aborted = true; end
end

MapData = Struct.new(:width, :height)
def load_data(name)
  return MapData.new(40, 30) if name.include?('Map002')
  {1 => MapInfo.new('Town'), 2 => MapInfo.new('Cave')}
end

if ENGINE == 'pe'
  class PokemonSystem; end
  class PokeBattle_Pokemon; end
  class Trainer
    attr_accessor :money, :party
    def heal_party; $healed = true; end
  end
  class Mon
    attr_accessor :level, :name
    def initialize(n); @name = n; @level = 5; end
    def calc_stats; @calced = true; end
    def egg?; false; end
  end
  class Bag
    def initialize; @q = Hash.new(0); end
    def quantity(id); @q[id]; end
    def add(id, n); @q[id] += n; end
    def remove(id, n); @q[id] -= n; end
  end
  module GameData
    module Item
      def self.each
        yield Struct.new(:id, :name).new(:POTION, 'Potion')
        yield Struct.new(:id, :name).new(:REPEL, 'Repel')
      end
    end
  end
  $wild = 0
  def pbEncounter(*_a); $wild += 1; true; end
  private :pbEncounter
  $player = Trainer.new
  $player.money = 100
  $player.party = [Mon.new('Pikachu')]
  $bag = Bag.new
end

$actors = [Game_Actor.new(1, 'Eric'), Game_Actor.new(2, 'Natalie')]
$game_party = Game_Party.new
$game_player = Game_Player.new
$game_map = Game_Map.new
$game_system = Game_System.new
$game_temp = Game_Temp.new
$game_troop = Game_Troop.new
$game_switches = {}
$game_variables = Hash.new(0)
$data_system = DataSystem.new([nil, 'Door open', ''], [nil, 'Coins', ''], 'G')
$data_items = [nil, Item.new(1, 'Potion'), Item.new(2, '')]
$data_weapons = [nil, Item.new(1, 'Sword')]
$data_armors = [nil, Item.new(1, 'Shield')]
$scene = Scene_Map.new

require_relative '../launcher/cheats'

def ask(cmd, row = 0)
  out = HardRPGCheats.request(cmd, row)
  reply = JSON.parse(out)
  @rows = reply['rows'] if reply['rows']
  (reply['set'] || []).each { |i, r| @rows[i] = r }
  reply
end

def row_index(label)
  i = @rows.index { |r| r['l'] == label || r['l'].start_with?(label) }
  raise "#{ENGINE}: no row #{label.inspect} in #{@rows.map { |r| r['l'] }.inspect}" unless i
  i
end

def press(label, button = 'cross')
  ask(button, row_index(label))
end

# Switches, variables, game rules: a warning page every time, then the page.
def careful(label)
  reply = press(label)
  check("#{label} warns") { reply['page'] == 'careful' && reply['focus'] == 2 }
  press('I know what I am changing')
end

def value(label)
  @rows[row_index(label)]['v']
end

# A number row: Cross opens the menu's editor; "set:<n>" is its answer.
def set(label, n)
  ask("set:#{n}", row_index(label))
end

def money
  ENGINE == 'pe' ? $player.money : $game_party.gold
end

# Opening: the instability warning first, once per run.
reply = ask('open')
check('warning first') { reply['page'] == 'warning' && reply['focus'] == 2 && @rows[0]['k'] == 'plain' }
check('warning back closes') { ask('back')['close'] }
check('warning again until accepted') { ask('open')['page'] == 'warning' }
reply = press('I understand, show the cheats')
check('main page') { reply['page'] == 'main' && reply['title'] == 'Cheats' && reply['focus'] == 0 }
check('subtitle names the map') { reply['subtitle'].include?('Map 1 Town (3, 4)') }
check('no Amount row') { @rows.none? { |r| r['l'] == 'Amount' } }
gold_row = @rows[row_index('Gold')]
check('gold is a number row') { gold_row['k'] == 'number' && gold_row['x'] == money && gold_row['lo'] == 0 }
check('gold limit is the game\'s own') { gold_row['hi'] == (ENGINE == 'ace' ? 99_999_999 : ENGINE == 'pe' ? 999_999 : 9_999_999) }
check('nav rows explain themselves') { @rows.select { |r| r['k'] == 'nav' }.all? { |r| r['n'] } }
before = money
press('Gold')
press('Gold', 'square')
press('Gold', 'left')
check('buttons on a number row change nothing') { money == before }
reply = set('Gold', 123_456)
check('gold set') { money == 123_456 && reply['m'] == 'Gold is now {}.' && reply['a'] == ['123,456'] }
check('gold row shows it') { value('Gold').start_with?('123,456') && @rows[row_index('Gold')]['x'] == 123_456 }
set('Gold', 5)
check('gold lowered') { money == 5 }
set('Gold', 10**12)
check('gold kept within the game\'s limit') { money == gold_row['hi'] }
set('Gold', -3)
check('gold never below 0') { money == 0 }

if ENGINE == 'pe'
  reply = press('Pokemon')
  check('pokemon page') { reply['page'] == 'pokemon' }
  set('Pikachu', 50)
  check('pokemon level') { $player.party[0].level == 50 && value('Pikachu') == 'Lv. 50' }
  set('Pikachu', 1000)
  check('pokemon level capped') { $player.party[0].level == 100 }
  press('Heal the party')
  check('pokemon healed') { $healed }
  reply = ask('back')
  check('back focuses the row that opened the page') { reply['page'] == 'main' && @rows[reply['focus']]['l'] == 'Pokemon' }
  press('Items')
  set('Potion', 30)
  check('bag set') { $bag.quantity(:POTION) == 30 && value('Potion') == '×30' }
  set('Potion', 4)
  check('bag lowered') { $bag.quantity(:POTION) == 4 }
  ask('back')
  press('Movement')
  press('No random battles')
  check('Essentials encounters stop') { pbEncounter(1) == false && $wild == 0 }
  press('Walk through walls')
  check('Essentials walk through walls') { $game_player.passable?(3, 4, 6, false) && !$game_player.passable?(0, 0, 4) }
  ask('back')
else
  # Party: god mode, levels, experience, stats, heal.
  press('Party')
  eric = $actors[0]
  press('God mode')
  eric.hp = 1
  check('god mode keeps HP full') { eric.hp == 100 }
  ENGINE == 'ace' ? eric.mp = 0 : eric.sp = 0
  check('god mode keeps MP full') { (ENGINE == 'ace' ? eric.mp : eric.sp) == 50 }
  $actors[1].hp = 1
  check('god mode is per member') { $actors[1].hp == 1 }
  check('level limits') { @rows[row_index('Level')]['lo'] == 1 && @rows[row_index('Level')]['hi'] == 99 }
  set('Level', 42)
  check('level set') { eric.level == 42 && value('Level') == '42' }
  set('Level', 500)
  check('level capped') { eric.level == 99 }
  set('Level', 0)
  check('level never below 1') { eric.level == 1 }
  set('Experience', 300)
  check('experience set') { eric.exp == 300 }
  press('Party member', 'right')
  check('second member chosen') { value('Party member').start_with?('Natalie') }
  press('Party member', 'cross')
  check('cross steps the choice too') { value('Party member').start_with?('Eric') }
  press('Party member', 'square')
  check('square leaves a choice alone') { value('Party member').start_with?('Eric') }
  press('Party member', 'right')
  press('Full heal')
  check('member healed') { $actors[1].hp == 100 }
  press('Whole party: god mode')
  $actors[1].hp = 3
  check('party god mode') { $actors[1].hp == 100 }
  stat = ENGINE == 'ace' ? 'Attack' : 'Strength'
  set(stat, 250)
  check('stat set') { value(stat) == '250' }
  set('Whole party: level', 7)
  check('whole party level') { $actors.all? { |a| a.level == 7 } }
  ask('back')

  # Rates: experience earned by battles is multiplied, our own changes are not.
  careful('Game rules')
  press('Experience', 'right') # ×2
  if ENGINE == 'ace'
    $actors[1].gain_exp(10)
    check('exp ×2') { $actors[1].exp >= 20 }
  else
    $actors[1].exp += 10
    check('exp ×2 on XP') { $actors[1].exp >= 20 }
  end
  press('Gold earned', 'right')
  $game_party.gain_gold(10)
  check('gold ×2') { $game_party.gold == 20 }
  $game_party.lose_gold(5)
  check('spending unchanged') { $game_party.gold == 15 }
  if ENGINE == 'ace'
    press('Items are not used up')
    $game_party.gain_item($data_items[1], 3)
    $game_party.consume_item($data_items[1])
    check('items kept') { $game_party.item_number($data_items[1]) == 3 }
  end
  press('Saving always allowed')
  $game_system.save_disabled = true
  $game_player.update
  check('saving allowed again') { $game_system.save_disabled == false }
  reply = press('Open the save screen')
  check('save screen resumes the game') { reply['resume'] }
  $game_player.update
  check('save screen opened') { ENGINE == 'ace' ? $called == Scene_Save : $scene.is_a?(Scene_Save) }
  $scene = Scene_Map.new
  ask('back')

  # Items, weapons, armor: blank names are left out; counts are typed.
  press('Items')
  check('blank item left out') { @rows.none? { |r| r['l'] == '' } }
  check('item limit is the game\'s') { @rows[row_index('Potion')]['hi'] == 99 }
  set('Potion', 12)
  check('item set') { value('Potion') == '×12' }
  set('Potion', 3)
  check('item lowered') { value('Potion') == '×3' }
  ask('back')
  press('Weapons')
  set('Set how many of every one', 99)
  check('every weapon') { value('Sword') == '×99' }
  set('Set how many of every one', 0)
  check('every weapon removed') { value('Sword') == '×0' }
  ask('back')

  # Battle.
  check('battle only in battle') { @rows[row_index('Battle')]['k'] == 'plain' }
  ENGINE == 'ace' ? $in_battle = true : $game_temp.in_battle = true
  if ENGINE == 'xp'
    class Window; attr_accessor :active, :visible; def initialize; @active = @visible = true; end; end unless defined?(Window)
    Arrow = Struct.new(:visible)
    class XpBattle < Scene_Battle
      attr_reader :won, :commands, :arrow
      def initialize; @phase = 3; @commands = @actor_command_window = Window.new; @arrow = @enemy_arrow = Arrow.new(true); end
      private def judge; @won = true; @phase = 5; end
    end
    $scene = XpBattle.new
  end
  ask('open')
  press('Battle')
  press('Enemies to 1 HP')
  check('enemies at 1 HP') { $game_troop.members.all? { |e| e.hp == 1 } }
  reply = press('Win the battle')
  check('enemies down') { $game_troop.members.all? { |e| e.hp == 0 } && reply['resume'] }
  if ENGINE == 'xp'
    # XP judges only after an action: while it waits for a command, the cheat
    # closes the input windows and runs the game's judge (Exit Fate crashed
    # picking a target among dead enemies).
    check('XP battle won at once') { $scene.won && !$scene.commands.active && !$scene.commands.visible && !$scene.arrow.visible }
  end
  press('Leave the battle')
  check('battle left') { ENGINE == 'ace' ? $aborted : $game_temp.battle_abort }
  ENGINE == 'ace' ? $in_battle = false : $game_temp.in_battle = false
  $scene = Scene_Map.new
  reply = ask('open')
  check('battle page closes after battle') { reply['page'] == 'main' }

  press('Movement')
  press('Walk through walls')
  check('walk through walls') { $game_player.passable?(3, 4, 6) }
  press('Walking speed', 'right')
  press('Walking speed', 'right')
  press('No random battles')
  $game_player.update
  check('speed kept') { $game_player.move_speed == 5 }
  check('no encounters') { $game_player.encounter_count >= 998 }
  ask('back')
end

# Teleport: the map from a list, the tile typed, kept spots.
press('Teleport')
check('x limit is the map width') { @rows[row_index('X')]['hi'] == 19 && @rows[row_index('Y')]['hi'] == 14 }
reply = press('Map')
check('map list') { reply['page'] == 'maps' && @rows.map { |r| r['l'] } == ['001 Town', '002 Cave'] && reply['focus'] == 0 }
reply = press('002 Cave')
check('map picked') { reply['page'] == 'teleport' && value('Map') == '2 Cave' }
check('limits follow the picked map') { @rows[row_index('X')]['hi'] == 39 && @rows[row_index('Y')]['hi'] == 29 }
set('X', 12)
set('Y', 99)
check('tile set') { value('X') == '12' && value('Y') == '29' }
reply = press('Go there')
check('teleport resumes') { reply['resume'] }
$game_player.update
if ENGINE == 'ace'
  check('reserve_transfer') { $game_player.reserved == [2, 12, 29, 2] }
else
  check('XP transfer') { $game_temp.player_transferring && $game_temp.player_new_map_id == 2 && $game_temp.player_new_x == 12 }
end
press('Spot 1', 'square')
check('spot kept') { value('Spot 1') == 'Map 1 Town · 3, 4' }
ask('back')

# Switches and variables.
careful('Switches')
check('switch count') { @rows[0]['k'] == 'plain' && @rows[0]['v'] == '0 / 2' }
check('switch names') { @rows[1]['l'] == '0001 Door open' && @rows[2]['l'] == '0002' && @rows[1]['r'] == 1 }
ask('cross', 1)
check('switch on') { $game_switches[1] == true && @rows[1]['on'] == true && $game_map.need_refresh }
check('switch count after') { ask('open') && @rows[0]['v'] == '1 / 2' }
ask('back')
careful('Variables')
check('variables are typed') { @rows[0]['k'] == 'number' && @rows[0]['lo'] < 0 }
ask('cross', 0)
ask('right', 0)
check('buttons leave a variable alone') { $game_variables[1] == 0 }
ask('set:-40', 0)
check('variable set') { $game_variables[1] == -40 && @rows[0]['v'] == '-40' }
ask('set:abc', 0)
check('bad number ignored') { $game_variables[1] == -40 }
ask('back')
reply = ask('back')
check('back from main closes') { reply['close'] }

# Older Essentials (Uranium, Insurgence) have no $game_party at all.
$game_party = nil
if ENGINE == 'pe'
  reply = JSON.parse(HardRPGCheats.request('open', 0))
  check('Essentials without $game_party') { reply['page'] == 'main' && reply['rows'][0]['l'] == 'Gold' }
end

# An error in a cheat is a message, never an exception.
$game_player = nil
reply = JSON.parse(HardRPGCheats.request('open', 0))
check('no game loaded') { reply['rows'][0]['l'].start_with?('Load or start') }
check('JSON escapes') { HardRPGCheats.encode("a\"b\\c\n") == '"a\\"b\\\\c\\u000a"' }
puts "cheats #{ENGINE}: OK"
