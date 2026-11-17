# HardRPG cheats (preload).
#
# The Cheats page of the in-game menu (L+R+Start) is drawn by the launcher's
# own UI, never by the game's windows, so it works in games with any menu
# scripts. The engine calls HardRPGCheats.request while the game is paused
# inside Graphics.update; this script owns the pages and their rows and
# answers with JSON (see request). The same protocol is answered for MV/MZ
# by launcher/cheats.js.
#
# Nothing is hooked until a cheat that needs a hook is turned on, and every
# switch-like cheat (god mode, walk through walls, speed...) lives in this
# module only: nothing is written into the game's objects that a save would
# keep, so a game saved with cheats on loads normally without them.
#
# Engines: XP, VX, VX Ace, and Pokemon Essentials (v16 to v21) for gold,
# items, party, movement, switches and variables.
module HardRPGCheats
  SPEEDS = [nil, 4, 5, 6].freeze # nil: the game's own speed
  MULTIPLIERS = [1, 2, 5, 10].freeze
  # Variables have no limit of their own; the number editor needs one.
  VARIABLE_LIMIT = 999_999_999

  @page = :main
  @stack = []
  @warned = false # the instability warning was accepted this run
  @careful = nil # the page the switches/variables/rules warning leads to
  @actor = 0
  @god = {} # actor id => true
  @through = false
  @speed = 0
  @no_encounters = false
  @exp_rate = 0
  @gold_rate = 0
  @free_items = false
  @allow_save = false
  @allow_menu = false
  @positions = [nil, nil, nil]
  @target = nil # [map, x, y] for Teleport
  @pending = nil # run on the map's next frame, after the menu closes
  @internal = false # our own changes skip the multipliers

  class << self
    attr_reader :god, :through, :no_encounters, :free_items, :internal

    # ---- protocol ----------------------------------------------------------
    # cmd: "open" (the page shown last, rebuilt), "back" (Circle), "cross",
    # "left", "right", "square", or "set:<integer>" (a number row's new
    # value, typed in the menu's number editor); row: the focused row (0 based).
    # The answer, as JSON:
    #   page, title, subtitle   the page now shown
    #   rows      [{k: kind, l: label, v: value, on: bool, n: note, r: 1,
    #               x: value, lo: min, hi: max}]
    #             kinds nav, action, toggle, choice, number, plain, danger;
    #             r: the label is game text (names), not translated;
    #             x/lo/hi (number rows): Cross opens the menu's editor on x,
    #             kept within lo..hi, and Cross there sends "set:<value>";
    #             sq: what Square does on this row (shown in the hint bar)
    #   focus     the row to focus (sent when the page changed)
    #   set       [[row, {...}], ...] rows that changed (instead of rows)
    #   m, a      a message with {} filled from a (translated by the menu)
    #   resume    close the menu and go back to the game
    #   close     leave the Cheats page
    def request(cmd, row)
      @reply = {}
      @changed = []
      @rebuild = false
      if cmd == 'open'
        unless page_usable?(@page)
          @page = :main
          @stack.clear
        end
        # Once per run, before any cheat: they can break a game.
        unless @warned || !ready?
          @page = :warning
          @stack.clear
        end
        @rebuild = true
      elsif cmd == 'back'
        if @stack.empty?
          @reply[:close] = true
          return encode(@reply)
        end
        @page, @focus = @stack.pop
        @rebuild = true
      else
        rows = build
        r = rows[row]
        if r && cmd.start_with?('set:')
          value = Integer(cmd[4..-1], 10) rescue nil
          if value && r[:k] == 'number' && r.key?(:hi)
            press(r, :set, [[value, r[:lo]].max, r[:hi]].min)
          end
        elsif r && r[:k] == 'choice'
          # Left and right step through the choices; Cross steps forward.
          press(r, cmd == 'left' ? :left : :right) unless cmd == 'square'
        elsif r && r[:k] != 'number' # number rows change only through the editor
          press(r, cmd.to_sym)
        end
      end
      rows = build
      if @rebuild
        @reply[:rows] = rows
        @reply[:focus] = @focus || 0
      else
        @reply[:set] = @changed.uniq.map { |i| [i, rows[i]] }.select { |_, v| v }
      end
      @focus = nil
      @reply[:page] = @page.to_s
      @reply[:title] = page_title
      @reply[:subtitle] = subtitle
      encode(@reply)
    rescue StandardError => e
      encode(m: 'That cheat does not work in this game ({}).', a: [e.class.name], page: @page.to_s,
             rows: safe_rows, focus: 0, title: 'Cheats', subtitle: '')
    end

    # ---- engine ------------------------------------------------------------
    def engine
      @engine ||= if Object.const_defined?(:PokemonSystem) &&
                     (Object.const_defined?(:PokeBattle_Pokemon) || Object.const_defined?(:GameData))
                    :pe
                  elsif Object.const_defined?(:RGSS_VERSION)
                    :ace
                  elsif defined?(RPG::BaseItem)
                    :vx
                  else
                    :xp
                  end
    end

    def pe?; engine == :pe; end
    def xp_like?; engine == :xp || engine == :pe; end

    # A game is loaded. Older Essentials (Uranium, Insurgence) never make a
    # $game_party: the trainer holds the party and the money.
    def ready?
      return false if $game_player.nil? || $game_map.nil?
      pe? ? !pe_player.nil? : !$game_party.nil?
    end

    def on_map?
      scene = current_scene
      Object.const_defined?(:Scene_Map) && scene.is_a?(Scene_Map)
    end

    def current_scene
      if Object.const_defined?(:SceneManager) && SceneManager.respond_to?(:scene)
        SceneManager.scene
      else
        $scene
      end
    end

    def in_battle?
      return $game_party.in_battle if $game_party.respond_to?(:in_battle)
      return $game_temp.in_battle if $game_temp.respond_to?(:in_battle)
      Object.const_defined?(:Scene_Battle) && current_scene.is_a?(Scene_Battle)
    end

    # ---- pages -------------------------------------------------------------
    def page_usable?(page)
      return false unless ready?
      case page
      when :battle then in_battle? && !pe?
      when :party, :weapons, :armors then !pe?
      when :pokemon then pe?
      else true
      end
    end

    def page_title
      {main: 'Cheats', party: 'Party', pokemon: 'Pokemon', items: 'Items', weapons: 'Weapons',
       armors: 'Armor', battle: 'Battle', move: 'Movement', teleport: 'Teleport',
       switches: 'Switches', variables: 'Variables', rules: 'Game rules', maps: 'Pick a map',
       warning: 'Before you use cheats', careful: 'Careful'}[@page] || 'Cheats'
    end

    def subtitle
      return '' unless ready?
      "#{engine_name} · #{fmt(gold)} #{currency} · #{map_label}"
    end

    def engine_name
      {xp: 'RPG Maker XP', vx: 'RPG Maker VX', ace: 'RPG Maker VX Ace', pe: 'Pokemon Essentials'}[engine]
    end

    def build
      @rows = []
      @handlers = []
      return [row(:plain, 'Load or start a game first.')] unless ready?
      send("rows_#{@page}")
      @rows
    end

    def safe_rows
      [row(:plain, 'Load or start a game first.')]
    end

    def row(kind, label, value = nil, on = nil, note = nil, raw = false, &blk)
      r = {k: kind.to_s, l: label}
      r[:v] = value.to_s if value
      r[:on] = on unless on.nil?
      r[:n] = note if note
      r[:r] = 1 if raw
      @rows << r
      @handlers[@rows.size - 1] = blk
      r
    end

    # A number the player types: Cross opens the menu's editor on value, kept
    # within lo..hi; the block gets the new value. shown: the text in the row.
    def number(label, value, lo, hi, shown = nil, raw = false, note = nil, &blk)
      value = value.is_a?(Integer) ? value : 0
      hi = [hi.to_i, lo].max
      r = row(:number, label, shown || fmt(value), nil, note || '<cross> Type the number you want.', raw) do |b, v|
        blk.call(v) if b == :set
      end
      r[:x] = [[value, lo].max, hi].min
      r[:lo] = lo
      r[:hi] = hi
      r
    end

    def press(_r, button, value = nil)
      index = @rows.index(_r)
      blk = @handlers[index]
      return unless blk
      @changed << index
      @current = index
      blk.call(button, value)
    end

    def open_page(page)
      @stack.push([@page, @current])
      @page = page
      @rebuild = true
    end

    # Switches, variables and game rules: a warning first, every time.
    def open_careful(page)
      open_page(:careful)
      @careful = page
    end

    def rows_warning
      row(:plain, 'Cheats can make a game unstable.')
      row(:plain, 'Save in a new slot before you change anything.')
      row(:primary, 'I understand, show the cheats', nil, nil,
          'Cheats change the running game directly, and the game may not expect it: events can break, ' \
          'and a save made afterwards keeps the change. Use them at your own discretion.') do
        @warned = true
        @page = :main
        @rebuild = true
        @focus = nil
      end
      @focus = 2
    end

    def rows_careful
      row(:plain, 'Only for players who know this game well.')
      row(:plain, 'A wrong value can break the story or the save.')
      row(:primary, 'I know what I am changing', nil, nil,
          'Games use these to track the story and events. Change one only if you know exactly why: ' \
          'a wrong value can lock you out of a quest or break the save for good. Save in a new slot first.') do
        @page = @careful || :main
        @rebuild = true
        @focus = nil
      end
      @focus = 2
    end

    # Every row on the page shows new values; the focus stays.
    def refresh!
      @rebuild = true
      @focus = @current
    end

    def message(text, *args)
      @reply[:m] = text
      @reply[:a] = args.map(&:to_s)
    end

    def resume_with(&blk)
      @pending = blk
      install_player_hook
      @reply[:resume] = true
    end

    # ---- main --------------------------------------------------------------
    def rows_main
      number('Gold', gold, 0, max_gold, "#{fmt(gold)} #{currency}", false,
             '<cross> Type how much gold you want. Up to {}, this game\'s own limit.') do |v|
        set_gold(v)
      end
      @rows.last[:a] = [fmt(max_gold)]
      if pe?
        row(:nav, 'Pokemon', nil, nil, 'Heal the party and set each Pokemon\'s level.') { open_page(:pokemon) }
        row(:nav, 'Items', nil, nil, 'Set how many of each item are in the bag.') { open_page(:items) }
      else
        row(:nav, 'Party', nil, nil, 'Each member: god mode, level, experience, healing and stats.') { open_page(:party) }
        row(:nav, 'Items', nil, nil, 'Set how many of each item you carry.') { open_page(:items) }
        row(:nav, 'Weapons', nil, nil, 'Set how many of each weapon you carry.') { open_page(:weapons) }
        row(:nav, 'Armor', nil, nil, 'Set how many of each armor you carry.') { open_page(:armors) }
        if in_battle?
          row(:nav, 'Battle', nil, nil, 'Win now, weaken the enemies, heal, or leave the battle.') { open_page(:battle) }
        else
          row(:plain, 'Battle', 'Only in battle', nil, 'Open the menu during a battle to win it, heal or leave.')
        end
      end
      row(:nav, 'Movement', nil, nil, 'Walk through walls, walking speed, no random battles.') { open_page(:move) }
      row(:nav, 'Teleport', nil, nil, 'Go to any map and tile, and keep spots to come back to.') { open_page(:teleport) }
      row(:nav, 'Switches', nil, nil, 'The game\'s on/off flags, by name. For players who know the game.') { open_careful(:switches) }
      row(:nav, 'Variables', nil, nil, 'The game\'s numbers, by name. For players who know the game.') { open_careful(:variables) }
      row(:nav, 'Game rules', nil, nil, 'More experience or gold, items not used up, saving and menu always allowed.') { open_careful(:rules) }
    end

    # ---- gold --------------------------------------------------------------
    def gold
      if pe?
        trainer = pe_player
        trainer ? trainer.money : 0
      else
        $game_party.gold
      end
    end

    def currency
      if pe?
        '$'
      elsif $data_system.respond_to?(:currency_unit)
        $data_system.currency_unit
      elsif $data_system.respond_to?(:words) && $data_system.words.respond_to?(:gold)
        $data_system.words.gold
      elsif defined?(Vocab) && Vocab.respond_to?(:gold)
        Vocab.gold
      else
        'G'
      end
    rescue StandardError
      'G'
    end

    def max_gold
      if pe?
        return Settings::MAX_MONEY if defined?(Settings::MAX_MONEY)
        return MAX_MONEY if defined?(MAX_MONEY)
        return MAXMONEY if defined?(MAXMONEY)
        999_999
      elsif $game_party.respond_to?(:max_gold)
        $game_party.max_gold
      else
        9_999_999
      end
    end

    # Through the game's own gain/lose, so its limit and any script watching
    # gold see the change.
    def set_gold(value)
      value = [[value, 0].max, max_gold].min
      delta = value - gold
      internally do
        if pe?
          pe_player.money = value
        elsif delta > 0
          $game_party.gain_gold(delta)
        elsif delta < 0
          $game_party.lose_gold(-delta)
        end
      end
      message('Gold is now {}.', fmt(gold))
    end

    # ---- party -------------------------------------------------------------
    def members
      party = $game_party
      if party.respond_to?(:all_members) then party.all_members
      elsif party.respond_to?(:actors) then party.actors
      else party.members
      end
    end

    def battle_members
      party = $game_party
      if party.respond_to?(:battle_members) then party.battle_members
      elsif party.respond_to?(:actors) then party.actors
      else party.members
      end
    end

    def actor_id(a)
      a.respond_to?(:id) ? a.id : a.instance_variable_get(:@actor_id)
    end

    def max_hp(a); a.respond_to?(:mhp) ? a.mhp : a.maxhp; end

    def max_mp(a)
      return a.mmp if a.respond_to?(:mmp)
      return a.maxmp if a.respond_to?(:maxmp)
      a.maxsp
    end

    def mp(a); a.respond_to?(:mp) ? a.mp : a.sp; end

    def max_level(a)
      return a.max_level if a.respond_to?(:max_level)
      $data_actors[actor_id(a)].final_level
    rescue StandardError
      99
    end

    def heal(a)
      if a.respond_to?(:recover_all)
        a.recover_all
      else
        a.hp = max_hp(a)
        a.respond_to?(:mp=) ? a.mp = max_mp(a) : a.sp = max_mp(a)
      end
      clear_states(a)
    end

    # Every state but death (a fainted member stays down until healed).
    def clear_states(a)
      death = a.respond_to?(:death_state_id) ? a.death_state_id : 1
      a.states.map { |s| s.respond_to?(:id) ? s.id : s }.each { |id| a.remove_state(id) unless id == death }
    end

    def set_level(a, level)
      level = [[level, 1].max, max_level(a)].min
      internally do
        if a.respond_to?(:change_level)
          a.change_level(level, false)
        else
          a.level = level
        end
      end
    end

    def set_exp(a, value)
      internally do
        if a.respond_to?(:change_exp)
          a.change_exp([value, 0].max, false)
        else
          a.exp = [value, 0].max
        end
      end
    end

    # The experience the last level needs (the most that means anything).
    def max_exp(a)
      top = max_level(a)
      if a.respond_to?(:exp_for_level) then a.exp_for_level(top)
      elsif a.instance_variable_defined?(:@exp_list) then a.instance_variable_get(:@exp_list)[top].to_i
      else 9_999_999
      end
    rescue StandardError
      9_999_999
    end

    # [label, getter, setter, highest value]
    def stats(a)
      case engine
      when :ace
        names = ['Max HP', 'Max MP', 'Attack', 'Defense', 'Magic attack', 'Magic defense', 'Agility', 'Luck']
        names.each_with_index.map do |n, i|
          top = a.respond_to?(:param_max) ? a.param_max(i) : 999_999
          [n, -> { a.param(i) }, ->(v) { a.add_param(i, v - a.param(i)) }, top]
        end
      when :vx
        [['Max HP', :maxhp, 999_999], ['Max MP', :maxmp, 9999], ['Attack', :atk, 999], ['Defense', :def, 999],
         ['Spirit', :spi, 999], ['Agility', :agi, 999]].map do |n, m, top|
          [n, -> { a.send(m) }, ->(v) { a.send("#{m}=", v) }, top]
        end
      else
        [['Max HP', :maxhp, 9999], ['Max SP', :maxsp, 9999], ['Strength', :str, 999], ['Dexterity', :dex, 999],
         ['Agility', :agi, 999], ['Intelligence', :int, 999]].map do |n, m, top|
          [n, -> { a.send(m) }, ->(v) { a.send("#{m}=", v) }, top]
        end
      end
    end

    def rows_party
      list = members
      return row(:plain, 'Nobody is in the party.') if list.empty?
      @actor = 0 if @actor >= list.size
      a = list[@actor]
      id = actor_id(a)
      row(:choice, 'Party member', "#{a.name} (#{@actor + 1}/#{list.size})", nil,
          'Left and right: whose rows these are.') do |b|
        next if b == :cross || b == :square
        @actor = (@actor + (b == :left ? -1 : 1)) % list.size
        refresh!
      end
      row(:toggle, 'God mode', nil, !!@god[id], 'HP and MP stay full and skills cost nothing.') do
        @god[id] = !@god[id]
        @god.delete(id) unless @god[id]
        install_god_hook
        heal(a) if @god[id]
        refresh!
        message(@god[id] ? 'God mode on for {}.' : 'God mode off for {}.', a.name)
      end
      number('Level', a.level, 1, max_level(a)) do |v|
        set_level(a, v)
        refresh!
        message('{} is now level {}.', a.name, a.level)
      end
      number('Experience', a.exp, 0, max_exp(a)) do |v|
        set_exp(a, v)
        refresh!
        message('{} has {} experience, level {}.', a.name, fmt(a.exp), a.level)
      end
      row(:action, 'Full heal', "#{a.hp}/#{max_hp(a)}") do
        heal(a)
        message('{} is fully healed.', a.name)
      end
      row(:action, 'Clear states') do
        clear_states(a)
        message('States cleared for {}.', a.name)
      end
      stats(a).each do |label, get, set, top|
        number(label, get.call, 1, top) do |v|
          internally { set.call(v) }
          refresh!
        end
      end
      row(:action, 'Whole party: full heal') do
        list.each { |m| heal(m) }
        message('The party is fully healed.')
      end
      all_god = list.all? { |m| @god[actor_id(m)] }
      row(:toggle, 'Whole party: god mode', nil, all_god) do
        list.each { |m| all_god ? @god.delete(actor_id(m)) : @god[actor_id(m)] = true }
        install_god_hook
        list.each { |m| heal(m) } unless all_god
        refresh!
        message(all_god ? 'God mode off for the party.' : 'God mode on for the party.')
      end
      top = list.map { |m| max_level(m) }.max
      number('Whole party: level', list.map(&:level).max, 1, top, nil, false,
             '<cross> Type the level for everyone in the party.') do |v|
        list.each { |m| set_level(m, v) }
        refresh!
        message('Everyone in the party is now level {}.', v.to_s)
      end
    end

    # ---- Essentials party --------------------------------------------------
    def pe_player
      return $player if defined?($player) && $player
      $Trainer
    end

    def pe_max_level
      return GameData::GrowthRate.max_level if defined?(GameData::GrowthRate) && GameData::GrowthRate.respond_to?(:max_level)
      return Settings::MAXIMUM_LEVEL if defined?(Settings::MAXIMUM_LEVEL)
      return PBExperience.maxLevel if defined?(PBExperience)
      100
    end

    def rows_pokemon
      party = pe_player ? pe_player.party : []
      row(:action, 'Heal the party') do
        if pe_player.respond_to?(:heal_party) then pe_player.heal_party
        elsif respond_to_global?(:pbHealAll) then pbHealAll
        else party.each { |p| p.heal if p.respond_to?(:heal) }
        end
        message('The party is fully healed.')
      end
      party.each do |pkmn|
        next if pkmn.nil? || (pkmn.respond_to?(:egg?) && pkmn.egg?) || (pkmn.respond_to?(:isEgg?) && pkmn.isEgg?)
        number(pkmn.name, pkmn.level, 1, pe_max_level, "Lv. #{pkmn.level}", true,
               '<cross> Type the level you want for this Pokemon.') do |v|
          pkmn.level = v
          pkmn.respond_to?(:calc_stats) ? pkmn.calc_stats : pkmn.calcStats
          refresh!
          message('{} is now level {}.', pkmn.name, pkmn.level)
        end
      end
    end

    def respond_to_global?(name)
      Object.private_method_defined?(name) || Object.method_defined?(name)
    end

    # ---- items -------------------------------------------------------------
    def database(kind)
      case kind
      when :items then $data_items
      when :weapons then $data_weapons
      when :armors then $data_armors
      end
    end

    def owned(kind, item)
      party = $game_party
      return party.item_number(item) unless xp_like?
      case kind
      when :items then party.item_number(item.id)
      when :weapons then party.weapon_number(item.id)
      else party.armor_number(item.id)
      end
    end

    def give(kind, item, n)
      party = $game_party
      if !xp_like?
        n >= 0 ? party.gain_item(item, n) : party.lose_item(item, -n)
      else
        case kind
        when :items then party.gain_item(item.id, n)
        when :weapons then party.gain_weapon(item.id, n)
        else party.gain_armor(item.id, n)
        end
      end
    end

    def rows_items; pe? ? pe_item_rows : item_rows(:items); end
    def rows_weapons; item_rows(:weapons); end
    def rows_armors; item_rows(:armors); end

    # The most of one item the party can carry (the game's own limit).
    def max_carry(item)
      party = $game_party
      party.respond_to?(:max_item_number) ? party.max_item_number(item) : 99
    rescue StandardError
      99
    end

    def set_owned(kind, item, n)
      give(kind, item, n - owned(kind, item))
    end

    def item_rows(kind)
      list = (database(kind) || []).compact.select { |i| i.name && !i.name.empty? }
      top = list.map { |i| max_carry(i) }.max || 99
      number('Set how many of every one', top, 0, top, '', false,
             '<cross> Type one number; every item on this page is set to it. 0 removes them all.') do |v|
        list.each { |i| set_owned(kind, i, [v, max_carry(i)].min) }
        refresh!
        message('You now have {} of each.', fmt(v))
      end
      list.each do |item|
        number(item.name, owned(kind, item), 0, max_carry(item), "×#{owned(kind, item)}", true,
               kind == :items ? '<cross> Type how many you want to have.' :
               '<cross> Type how many you want to have. Equip lists it only for characters whose class can use it.') do |v|
          set_owned(kind, item, v)
          message('{}: you now have {}.', item.name, fmt(owned(kind, item)))
        end
      end
    end

    # Essentials: v19+ GameData::Item and $bag / $PokemonBag; older PBItems.
    def pe_items
      @pe_items ||= if defined?(GameData::Item) && GameData::Item.respond_to?(:each)
                      list = []
                      GameData::Item.each { |i| list << [i.id, i.name] }
                      list
                    elsif defined?(PBItems)
                      max = PBItems.respond_to?(:maxValue) ? PBItems.maxValue : 999
                      (1..max).map { |i| [i, (PBItems.getName(i) rescue nil)] }.select { |_, n| n && !n.empty? }
                    else
                      []
                    end
    end

    def pe_bag
      defined?($bag) && $bag ? $bag : $PokemonBag
    end

    def pe_quantity(id)
      bag = pe_bag
      bag.respond_to?(:quantity) ? bag.quantity(id) : bag.pbQuantity(id)
    end

    def pe_give(id, n)
      bag = pe_bag
      if n >= 0
        bag.respond_to?(:add) ? bag.add(id, n) : bag.pbStoreItem(id, n)
      else
        n = [-n, pe_quantity(id)].min
        return if n <= 0
        bag.respond_to?(:remove) ? bag.remove(id, n) : bag.pbDeleteItem(id, n)
      end
    end

    def pe_item_rows
      pe_items.each do |id, name|
        have = pe_quantity(id)
        number(name, have, 0, 999, "×#{have}", true, '<cross> Type how many you want in the bag.') do |v|
          pe_give(id, v - pe_quantity(id))
          message('{}: you now have {}.', name, fmt(pe_quantity(id)))
        end
      end
    end

    # ---- battle ------------------------------------------------------------
    def enemies
      t = $game_troop
      t.respond_to?(:enemies) ? t.enemies : t.members
    end

    # XP checks for a win only after an action or a battle event; VX and Ace
    # check every frame. While XP waits for a command, the battle would go on
    # with no enemy to target (Exit Fate: NoMethodError picking a target).
    # Close the command windows and target arrows, then run the game's own
    # judge, which starts the victory phase.
    def xp_win_now
      scene = current_scene
      return unless Object.const_defined?(:Scene_Battle) && scene.is_a?(Scene_Battle)
      phase = scene.instance_variable_get(:@phase)
      return if phase == 4 || phase == 5 # an action runs: its next step judges
      scene.instance_variables.each do |name|
        v = scene.instance_variable_get(name)
        if v.is_a?(Window) && v.active
          v.active = false
          v.visible = false
        elsif name.to_s.end_with?('_arrow') && v.respond_to?(:visible=)
          v.visible = false
        end
      end
      help = scene.instance_variable_get(:@help_window)
      help.visible = false if help.respond_to?(:visible=)
      scene.send(:judge) if scene.respond_to?(:judge, true)
    end

    def rows_battle
      unless in_battle?
        row(:plain, 'Only in battle')
        return
      end
      row(:action, 'Win the battle') do
        enemies.each do |e|
          next if e.respond_to?(:exist?) && !e.exist?
          e.hp = 0
          e.perform_collapse_effect if e.respond_to?(:perform_collapse_effect)
          e.perform_collapse if e.respond_to?(:perform_collapse) && !e.respond_to?(:perform_collapse_effect)
        end
        xp_win_now if xp_like?
        @reply[:resume] = true
      end
      row(:action, 'Enemies to 1 HP') do
        enemies.each { |e| e.hp = 1 if e.hp > 1 }
        message('Every enemy has 1 HP.')
      end
      row(:action, 'Full heal the party') do
        battle_members.each { |m| heal(m) }
        message('The party is fully healed.')
      end
      if engine == :ace || engine == :xp
        row(:action, 'Leave the battle') do
          if engine == :ace then BattleManager.abort
          else $game_temp.battle_abort = true
          end
          @reply[:resume] = true
        end
      end
    end

    # ---- movement ----------------------------------------------------------
    def rows_move
      row(:toggle, 'Walk through walls', nil, @through, 'Walls, water and people no longer stop you.') do
        @through = !@through
        install_player_hook
        install_through_hook
      end
      speed = SPEEDS[@speed]
      row(:choice, 'Walking speed', speed.nil? ? "Game's own" : ['Normal', 'Fast', 'Faster'][speed - 4],
          nil, 'Kept even when the game changes it.') do |b|
        @speed = (@speed + (b == :left ? -1 : 1)) % SPEEDS.size
        install_player_hook
      end
      unless pe? && !respond_to_global?(:pbEncounter)
        row(:toggle, 'No random battles', nil, @no_encounters, 'Random encounters stop; battles set by events still happen.') do
          @no_encounters = !@no_encounters
          install_player_hook
          install_encounter_hook if pe?
        end
      end
    end

    # ---- teleport ----------------------------------------------------------
    def map_infos
      @map_infos ||= begin
        ext = {xp: 'rxdata', pe: 'rxdata', vx: 'rvdata', ace: 'rvdata2'}[engine]
        infos = load_data("Data/MapInfos.#{ext}")
        infos.keys.sort.map { |id| [id, infos[id].name] }
      rescue StandardError
        []
      end
    end

    def map_name(id)
      found = map_infos.assoc(id)
      found ? found[1] : ''
    end

    def map_label
      id = $game_map.map_id
      name = map_name(id)
      base = "Map #{id}"
      base += " #{name}" unless name.empty?
      "#{base} (#{$game_player.x}, #{$game_player.y})"
    end

    def place(pos)
      return 'Empty' unless pos
      name = map_name(pos[0])
      "Map #{pos[0]}#{name.empty? ? '' : ' ' + name} · #{pos[1]}, #{pos[2]}"
    end

    def rows_teleport
      @target ||= [$game_map.map_id, $game_player.x, $game_player.y]
      ids = map_infos.map(&:first)
      name = map_name(@target[0])
      row(:nav, 'Map', name.empty? ? @target[0] : "#{@target[0]} #{name}", nil,
          '<cross> Pick the map from a list of its names.') do
        if ids.empty?
          message('This game has no map list.')
        else
          open_page(:maps)
          @focus = ids.index(@target[0]) || 0
        end
      end
      w, h = map_size(@target[0])
      %w[X Y].each_with_index do |axis, i|
        number(axis, @target[i + 1], 0, [(i.zero? ? w : h) - 1, 0].max, nil, false,
               '<cross> Type the tile to arrive on. 0 is the left (X) or top (Y) edge.') do |v|
          @target[i + 1] = v
        end
      end
      row(:primary, 'Go there', nil, nil, 'Closes the menu and moves you to the map and tile above.') { teleport(*@target) }
      row(:action, 'Back to where I am', nil, nil, 'Sets the rows above to the place you are now.') do
        @target = [$game_map.map_id, $game_player.x, $game_player.y]
        refresh!
      end
      3.times do |i|
        row(:action, "Spot #{i + 1}", place(@positions[i]), nil,
            '<cross> Go to this spot. <square> Save where you are now into it. Kept until HardRPG closes.') do |b|
          if b == :square
            @positions[i] = [$game_map.map_id, $game_player.x, $game_player.y]
            message('Spot {} saved: {}.', i + 1, place(@positions[i]))
          elsif @positions[i]
            teleport(*@positions[i])
          else
            message('Spot {} is empty. <square> saves where you are now into it.', i + 1)
          end
        end
        @rows.last[:sq] = 'Save spot here'
      end
    end

    # Width and height in tiles; the current map from memory, others from
    # their data file.
    def map_size(id)
      return [$game_map.width, $game_map.height] if id == $game_map.map_id
      @map_sizes ||= {}
      @map_sizes[id] ||= begin
        ext = {xp: 'rxdata', pe: 'rxdata', vx: 'rvdata', ace: 'rvdata2'}[engine]
        m = load_data(format('Data/Map%03d.%s', id, ext))
        [m.width, m.height]
      rescue StandardError
        [500, 500]
      end
    end

    # Every map by name: Cross picks it for Teleport.
    def rows_maps
      map_infos.each do |id, name|
        row(:action, format('%03d', id) + (name.empty? ? '' : " #{name}"), nil, @target && @target[0] == id, nil, true) do
          w, h = map_size(id)
          @target = [id, [@target[1], w - 1].min, [@target[2], h - 1].min]
          @page, @focus = @stack.pop
          @rebuild = true
        end
      end
    end

    def teleport(map, x, y)
      unless on_map?
        message('Teleport works on the map only.')
        return
      end
      dir = $game_player.direction
      resume_with do
        if $game_player.respond_to?(:reserve_transfer)
          $game_player.reserve_transfer(map, x, y, dir)
        else
          $game_temp.player_transferring = true
          $game_temp.player_new_map_id = map
          $game_temp.player_new_x = x
          $game_temp.player_new_y = y
          $game_temp.player_new_direction = dir
        end
      end
    end

    # ---- switches and variables -------------------------------------------
    def names(list, id)
      n = list[id]
      n.nil? || n.empty? ? '' : " #{n}"
    end

    def rows_switches
      list = $data_system.switches
      on = (1...list.size).count { |id| $game_switches[id] }
      row(:plain, 'Switches that are on', "#{fmt(on)} / #{fmt(list.size - 1)}")
      (1...list.size).each do |id|
        row(:toggle, format('%04d', id) + names(list, id), nil, !!$game_switches[id], nil, true) do
          $game_switches[id] = !$game_switches[id]
          $game_map.need_refresh = true
        end
      end
    end

    def rows_variables
      list = $data_system.variables
      (1...list.size).each do |id|
        value = $game_variables[id]
        shown = value.is_a?(Integer) ? fmt(value) : value.inspect[0, 24]
        number(format('%04d', id) + names(list, id), value, -VARIABLE_LIMIT, VARIABLE_LIMIT, shown, true,
               '<cross> Type the value. The game reads it at once; a wrong value can break events.') do |v|
          $game_variables[id] = v
          $game_map.need_refresh = true
        end
      end
    end

    # ---- game rules --------------------------------------------------------
    def rows_rules
      row(:choice, 'Experience', "×#{MULTIPLIERS[@exp_rate]}", nil, 'Multiplies the experience the party earns.') do |b|
        @exp_rate = (@exp_rate + (b == :left ? -1 : 1)) % MULTIPLIERS.size
        install_rate_hooks
      end
      row(:choice, 'Gold earned', "×#{MULTIPLIERS[@gold_rate]}", nil, 'Multiplies the gold the party earns. Spending is not changed.') do |b|
        @gold_rate = (@gold_rate + (b == :left ? -1 : 1)) % MULTIPLIERS.size
        install_rate_hooks
      end
      if engine == :vx || engine == :ace
        row(:toggle, 'Items are not used up', nil, @free_items, 'Using an item keeps it.') do
          @free_items = !@free_items
          install_item_hook
        end
      end
      unless pe?
        row(:toggle, 'Saving always allowed', nil, @allow_save, 'For places where the game turned saving off.') do
          @allow_save = !@allow_save
          install_player_hook
        end
        row(:toggle, 'Menu always allowed', nil, @allow_menu, 'For places where the game turned its menu off.') do
          @allow_menu = !@allow_menu
          install_player_hook
        end
        row(:action, 'Open the save screen', nil, nil, 'Closes this menu and opens the game\'s save screen, even where saving is off.') do
          unless on_map?
            message('Opens from the map only.')
            next
          end
          resume_with { open_save_screen }
        end
      end
    end

    def open_save_screen
      $game_system.save_disabled = false if $game_system.respond_to?(:save_disabled=)
      case engine
      when :ace then SceneManager.call(Scene_Save)
      when :vx then $scene = Scene_File.new(true, false, false)
      else $scene = Scene_Save.new
      end
    end

    # ---- hooks -------------------------------------------------------------
    def internally
      was = @internal
      @internal = true
      yield
    ensure
      @internal = was
    end

    def multiplier(kind)
      MULTIPLIERS[kind == :exp ? @exp_rate : @gold_rate]
    end

    def speed
      SPEEDS[@speed]
    end

    # Called by Game_Player#update every frame once a hook is in.
    def player_tick(player)
      if @pending
        job = @pending
        @pending = nil
        job.call
      end
      player.instance_variable_set(:@move_speed, speed) if speed
      if @no_encounters && player.instance_variable_defined?(:@encounter_count)
        count = player.instance_variable_get(:@encounter_count)
        player.instance_variable_set(:@encounter_count, 999) if count.is_a?(Integer) && count < 999
      end
      sys = $game_system
      sys.save_disabled = false if @allow_save && sys.respond_to?(:save_disabled=)
      sys.menu_disabled = false if @allow_menu && sys.respond_to?(:menu_disabled=)
    rescue StandardError
      nil
    end

    def hook(klass, mod)
      klass.prepend(mod) unless klass.ancestors.include?(mod)
    end

    def install_player_hook
      hook(Game_Player, PlayerHook)
    end

    def install_through_hook
      if Game_Player.method_defined?(:debug_through?) && !xp_like?
        hook(Game_Player, ThroughDebugHook)
      else
        hook(Game_Player, ThroughPassableHook)
      end
    end

    def install_god_hook
      hook(Game_Actor, GodHook)
    end

    def install_item_hook
      hook(Game_Party, ItemHook) if Game_Party.method_defined?(:consume_item)
    end

    def install_rate_hooks
      hook(Game_Party, GoldHook)
      hook(Game_Actor, Game_Actor.method_defined?(:gain_exp) ? ExpGainHook : ExpSetHook)
    end

    def install_encounter_hook
      return if @encounter_hooked || !respond_to_global?(:pbEncounter)
      Object.send(:alias_method, :hardrpg_cheats_pbEncounter, :pbEncounter)
      Object.send(:define_method, :pbEncounter) do |*args|
        return false if HardRPGCheats.no_encounters
        hardrpg_cheats_pbEncounter(*args)
      end
      Object.send(:private, :pbEncounter)
      @encounter_hooked = true
    end

    def god?(battler)
      !@god.empty? && battler.respond_to?(:actor?) && battler.actor? && @god[actor_id(battler)]
    rescue StandardError
      false
    end

    # ---- JSON --------------------------------------------------------------
    def fmt(n)
      return n.to_s unless n.is_a?(Integer)
      s = n.abs.to_s.reverse.scan(/\d{1,3}/).join(',').reverse
      n < 0 ? "-#{s}" : s
    end

    def encode(v)
      case v
      when Hash then '{' + v.map { |k, x| encode(k.to_s) + ':' + encode(x) }.join(',') + '}'
      when Array then '[' + v.map { |x| encode(x) }.join(',') + ']'
      when String, Symbol then quote(v.to_s)
      when Integer, Float then v.to_s
      when true then 'true'
      when false then 'false'
      when nil then 'null'
      else quote(v.to_s)
      end
    end

    def quote(s)
      s = s.dup.force_encoding('UTF-8')
      s = s.scrub('?') unless s.valid_encoding?
      '"' + s.gsub(/["\\\u0000-\u001f]/) { |c| c == '"' ? '\\"' : c == '\\' ? '\\\\' : format('\\u%04x', c.ord) } + '"'
    end
  end

  module PlayerHook
    def update
      HardRPGCheats.player_tick(self)
      super
    end
  end

  # VX and VX Ace: the engine's own Ctrl walk-through check.
  module ThroughDebugHook
    def debug_through?
      HardRPGCheats.through || super
    end
  end

  # XP and Essentials: the player's passability.
  module ThroughPassableHook
    def passable?(*args)
      if HardRPGCheats.through && args.size >= 3
        x, y, d = args
        nx = x + (d == 6 ? 1 : d == 4 ? -1 : 0)
        ny = y + (d == 2 ? 1 : d == 8 ? -1 : 0)
        return $game_map.valid?(nx, ny)
      end
      super
    end
  end

  module GodHook
    def hp=(value)
      value = HardRPGCheats.max_hp(self) if HardRPGCheats.god?(self)
      super(value)
    end

    def mp=(value)
      value = HardRPGCheats.max_mp(self) if HardRPGCheats.god?(self)
      super(value)
    end

    def sp=(value)
      value = HardRPGCheats.max_mp(self) if HardRPGCheats.god?(self)
      super(value)
    end

    def tp=(value)
      value = max_tp if HardRPGCheats.god?(self) && respond_to?(:max_tp)
      super(value)
    end
  end

  module ItemHook
    def consume_item(item)
      return if HardRPGCheats.free_items
      super
    end
  end

  module GoldHook
    def gain_gold(n)
      n *= HardRPGCheats.multiplier(:gold) if n.is_a?(Integer) && n > 0 && !HardRPGCheats.internal
      super(n)
    end
  end

  module ExpGainHook
    def gain_exp(exp, *rest)
      exp *= HardRPGCheats.multiplier(:exp) if exp.is_a?(Integer) && exp > 0 && !HardRPGCheats.internal
      super(exp, *rest)
    end
  end

  module ExpSetHook
    def exp=(value)
      old = @exp.to_i
      rate = HardRPGCheats.multiplier(:exp)
      value = old + (value - old) * rate if rate > 1 && value.is_a?(Integer) && value > old && !HardRPGCheats.internal
      super(value)
    end
  end
end
