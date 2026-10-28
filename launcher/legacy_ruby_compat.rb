# Legacy Ruby compatibility (preload). RGSS1 shipped Ruby 1.8 and RGSS2/3
# shipped Ruby 1.9; HardRPG runs every game on Ruby 3.1. This file restores
# what shipped games use that Ruby has since removed.

# Hash#index was removed in Ruby 3.0 (it is Hash#key). PALETTA's pathfinding
# script raised NoMethodError when an event's move route searched a path.
class Hash
  def index(value)
    key(value)
  end unless method_defined?(:index)
end

# Thread.critical was removed in Ruby 1.9. WF-RGSS sets it around the
# disposal pass of every scene (Shin Megami Tensei VX). Game scripts run on
# one Ruby thread here, so keep the flag and nothing else.
class Thread
  class << self
    def critical
      @hardrpg_critical || false
    end unless method_defined?(:critical)

    def critical=(value)
      @hardrpg_critical = value
    end unless method_defined?(:critical=)
  end
end

# Ruby 1.8 gave every object to_a, returning [self]; 1.9 removed it.
# Shin Megami Tensei VX's state scripts call it on state ids (Integer), and
# New Game quit on the NoMethodError. Classes with their own to_a (Array,
# Hash, Range, Struct, nil, anything Enumerable) keep theirs. RGSS1/2 only:
# VX Ace already ran on Ruby 1.9.
class Object
  def to_a
    [self]
  end
end unless Kernel.respond_to?(:rgss_main, true) || Object.method_defined?(:to_a)

# WF-RGSS, a Japanese script base, tells RPG Maker versions apart by probing
# Ruby: `defined? BasicObject` means VX Ace and `defined? Graphics.resize_screen`
# means VX or later. On Ruby 3.1 BasicObject always exists and mkxp-z gives
# every version resize_screen, so XP and VX games took the VX Ace branch and
# called rgss_main, which only VX Ace has (Shin Megami Tensei VX stopped at
# boot). When a game defines these probes on Kernel, answer them from the
# version the engine runs.
module HardRPGRgssProbes
  ANSWERS = {
    rpgvxace?: Kernel.respond_to?(:rgss_main, true), # the engine defines it for RGSS3 only
    rpgvx?: !defined?(RPG::Cache)                     # the engine defines it for RGSS1 only
  }.freeze

  private

  def method_added(name)
    super
    return unless equal?(Kernel) && ANSWERS.key?(name) && !@hardrpg_probe_busy
    @hardrpg_probe_busy = true
    begin
      hidden = private_method_defined?(name)
      answer = ANSWERS[name]
      define_method(name) { answer }
      private(name) if hidden
    ensure
      @hardrpg_probe_busy = false
    end
  end
end
Kernel.singleton_class.prepend(HardRPGRgssProbes)

# Ruby 1.8 (RGSS1/2) let scripts clear the last error with `$! = nil`; Ruby
# 1.9 made $! read-only, and Ruby 3.1 raises NameError when that line runs.
# WF-RGSS does it at the end of every scene, so Shin Megami Tensei VX stopped
# at its title screen. The engine decodes every section into $RGSS_SCRIPTS
# before preloads run and evaluates that text afterwards, so drop the
# assignment there. Ruby 3.1 already clears $! once a rescue has finished.
module HardRPGLegacyErrinfo
  CLEAR = /\$!\s*=\s*nil\b/.freeze

  def self.rewrite(source)
    return nil unless source.include?('$!') && source =~ CLEAR
    source.gsub(CLEAR, 'nil')
  end

  def self.apply(scripts)
    return unless scripts.is_a?(Array)
    scripts.each do |section|
      next unless section.is_a?(Array) && section[3].is_a?(String)
      fixed = rewrite(section[3])
      section[3] = fixed if fixed
    end
  end
end
HardRPGLegacyErrinfo.apply($RGSS_SCRIPTS) unless Kernel.respond_to?(:rgss_main, true)

# Ruby 3.1's peephole pass sometimes crashes the Vita build while compiling
# a large script (iseq_peephole_optimize). Shin Megami Tensei VX hit it at
# boot about one launch in three, on stock 0.4.5 too. Turned off for games on
# WF-RGSS only, so other games' scripts compile exactly as before.
module HardRPGCompileGuard
  def self.wf_rgss?(scripts)
    scripts.is_a?(Array) && scripts.any? do |section|
      section.is_a?(Array) && [section[1], section[3]].any? { |s| s.is_a?(String) && s.include?('WF-RGSS') }
    end
  end
end
if HardRPGCompileGuard.wf_rgss?($RGSS_SCRIPTS)
  RubyVM::InstructionSequence.compile_option = { peephole_optimization: false }
end

# Saba's "3D Dungeon" script (Shin Megami Tensei VX and other VX dungeon
# crawlers) redraws the whole view, four 1064x416 bitmaps and ~33 large wall
# copies (~80 ms on the Vita), every time Dungeon_Sprite#refresh_3d is called.
# Holding a direction into a wall calls it every frame, often twice: a
# half-step view that is overwritten at once, then the same view as the frame
# before. The drawing is held until the frame is shown, so only the last
# request in a frame is drawn, and it is skipped when everything it reads is
# the same as for the view already drawn. If the player moved after the last
# request, that request is drawn with the player as it was then.
#
# The layers are twice the screen width only for the turn animation; between
# turns each layer sprite shows the middle half. A view drawn while not turning
# only draws inside the part the sprites show (HardRPGDungeonClip). Should a
# sprite ever show more before the next draw, the view is drawn again in full.
module HardRPGDungeonClip
  # Extended onto the dungeon's layer bitmaps. @hardrpg_clip is [left, right)
  # while a view is drawn clipped, nil otherwise. Each op is cut only where the
  # cut copies exactly the same pixels; anything else runs as written.
  def blt(x, y, src, rect, opacity = 255)
    c = @hardrpg_clip
    return super unless c && (x.is_a?(Integer) || x.is_a?(Float)) && rect.is_a?(Rect) &&
                        src.is_a?(Bitmap) && !src.disposed?
    rx = rect.x; ry = rect.y; rw = rect.width; rh = rect.height
    return super unless rw > 0 && rh > 0 && rx >= 0 && ry >= 0 && rx + rw <= src.width && ry + rh <= src.height
    x0 = x.to_i
    a = x0 > c[0] ? x0 : c[0]
    b = x0 + rw < c[1] ? x0 + rw : c[1]
    return self if a >= b
    return super if a == x0 && b == x0 + rw
    super(a, y, src, Rect.new(rx + a - x0, ry, b - a, rh), opacity)
  end

  # Only a one-pixel source (a flat tint) inside the bitmap: the same colour
  # lands on every covered pixel however much of the rect is drawn.
  def stretch_blt(drect, src, srect, opacity = 255)
    c = @hardrpg_clip
    return super unless c && drect.is_a?(Rect) && srect.is_a?(Rect) && src.is_a?(Bitmap) && !src.disposed? &&
                        src.width == 1 && src.height == 1 && srect.x == 0 && srect.y == 0 &&
                        srect.width == 1 && srect.height == 1
    x0 = drect.x; w = drect.width
    return super unless w > 0 && drect.height > 0 && x0 >= 0 && drect.y >= 0 &&
                        x0 + w <= width && drect.y + drect.height <= height
    a = x0 > c[0] ? x0 : c[0]
    b = x0 + w < c[1] ? x0 + w : c[1]
    return self if a >= b
    return super if a == x0 && b == x0 + w
    super(Rect.new(a, drect.y, b - a, drect.height), src, srect, opacity)
  end

  def fill_rect(*args)
    c = @hardrpg_clip
    return super unless c
    r = HardRPGDungeonClip.cut(c, args, 1)
    r == :skip ? self : (r ? super(*r) : super)
  end

  def clear_rect(*args)
    c = @hardrpg_clip
    return super unless c
    r = HardRPGDungeonClip.cut(c, args, 0)
    r == :skip ? self : (r ? super(*r) : super)
  end

  # (x, y, w, h, *rest) or (rect, *rest) cut to the window: new args, :skip
  # when nothing is left, nil to run the call unchanged.
  def self.cut(c, args, rest)
    if args.size == 1 + rest && args[0].is_a?(Rect)
      r = args[0]
      x = r.x; y = r.y; w = r.width; h = r.height
    elsif args.size == 4 + rest && args[0, 4].all? { |v| v.is_a?(Integer) || v.is_a?(Float) }
      x, y, w, h = args[0, 4].map(&:to_i)
    else
      return nil
    end
    return nil unless w > 0 && h > 0
    a = x > c[0] ? x : c[0]
    b = x + w < c[1] ? x + w : c[1]
    return :skip if a >= b
    return nil if a == x && b == x + w
    rest.zero? ? [a, y, b - a, h] : [a, y, b - a, h, args[-1]]
  end
end

module HardRPGDungeonRedraw
  PLAYER = [:@x, :@y, :@direction, :@move_front, :@rotation_left, :@rotation_right].freeze
  @pending = []
  @installed = false
  @clip = true
  @clipped = nil

  class << self
    attr_accessor :clip
  end

  def self.clipped(sprite)
    @clipped = sprite
  end

  def self.uses_script?(scripts)
    scripts.is_a?(Array) && scripts.any? do |section|
      text = section.is_a?(Array) && section[3]
      text.is_a?(String) && text.include?('class Dungeon_Sprite') && text.include?('def refresh_3d')
    end
  end

  def self.compatible?
    defined?(::Dungeon_Sprite) && defined?(::Game_Player) &&
      ::Dungeon_Sprite.method_defined?(:refresh_3d) &&
      [:move_front?, :rotation_left?, :rotation_right?].all? { |m| ::Game_Player.method_defined?(m) }
  end

  def self.install
    return true if @installed
    return false unless compatible?
    @installed = true
    ::Dungeon_Sprite.class_eval do
      alias_method :__hardrpg_refresh_3d, :refresh_3d
      def refresh_3d
        state = (HardRPGDungeonRedraw.capture rescue nil)
        unless state
          @hardrpg_window = nil
          @hardrpg_drawn_key = nil
          return __hardrpg_refresh_3d
        end
        @hardrpg_pending_state = state
        HardRPGDungeonRedraw.defer(self)
        nil
      end

      def hardrpg_flush
        state = @hardrpg_pending_state
        return unless state
        @hardrpg_pending_state = nil
        return if @sprites.nil? || @bitmaps.nil? || @bitmaps.any?(&:disposed?)
        HardRPGDungeonRedraw.with_state(state) do
          key = (hardrpg_view_key rescue nil)
          unless key && key == @hardrpg_drawn_key
            @hardrpg_drawn_key = nil
            hardrpg_draw(HardRPGDungeonRedraw.clip && hardrpg_window, state)
            @hardrpg_drawn_key = key
          end
        end
      end

      # The part of the layers the sprites show, [left, right), when the view
      # is not turning and every layer shows the same columns; nil otherwise.
      def hardrpg_window
        player = $game_player
        return nil if player.rotation_right? || player.rotation_left?
        return nil unless @start_ox.is_a?(Integer) && @sprites.size == @bitmaps.size
        window = nil
        @sprites.each_with_index do |sprite, i|
          return nil if sprite.disposed? || !sprite.bitmap.equal?(@bitmaps[i])
          return nil if sprite.respond_to?(:wave_amp) && sprite.wave_amp != 0
          r = sprite.src_rect
          return nil unless r.x == @start_ox && r.width > 0
          w = [r.x, r.x + r.width]
          return nil if window && window != w
          window = w
        end
        window
      end

      def hardrpg_draw(window, state)
        @hardrpg_window = nil
        if window
          @bitmaps.each do |b|
            b.extend(HardRPGDungeonClip) unless b.is_a?(HardRPGDungeonClip)
            b.instance_variable_set(:@hardrpg_clip, window)
          end
        end
        begin
          __hardrpg_refresh_3d
        ensure
          @bitmaps.each { |b| b.instance_variable_set(:@hardrpg_clip, nil) } if window
        end
        return unless window
        @hardrpg_window = window
        @hardrpg_window_state = state
        HardRPGDungeonRedraw.clipped(self)
      end

      # After a clipped draw: true while every sprite still shows only the
      # columns that were drawn.
      def hardrpg_window_shown?
        window = @hardrpg_window
        return true unless window
        return true if @sprites.nil? || @bitmaps.nil? || @bitmaps.any?(&:disposed?)
        @sprites.each_with_index.all? do |sprite, i|
          next true if sprite.disposed? || !sprite.visible
          r = sprite.src_rect
          sprite.bitmap.equal?(@bitmaps[i]) && r.x >= window[0] && r.x + r.width <= window[1] &&
            !(sprite.respond_to?(:wave_amp) && sprite.wave_amp != 0)
        end
      end

      # Draws the last clipped view again in full.
      def hardrpg_unclip
        state = @hardrpg_window_state
        @hardrpg_window = nil
        return if @sprites.nil? || @bitmaps.nil? || @bitmaps.any?(&:disposed?)
        if state
          HardRPGDungeonRedraw.with_state(state) { __hardrpg_refresh_3d }
        else
          __hardrpg_refresh_3d
        end
      end

      # Everything refresh_3d reads, so an equal key draws equal pixels.
      def hardrpg_view_key
        map = $game_map
        player = $game_player
        events = []
        map.events.each_value do |e|
          events << e.id << e.x << e.y << e.direction << e.through << e.transparent <<
            e.instance_variable_get(:@page).__id__ << e.tile_id << e.character_name << e.character_index
        end
        dungeon = $dungeon
        colour = ->(c) { c.respond_to?(:red) ? [c.red, c.green, c.blue, c.alpha] : c }
        switch = defined?(Saba::Three_D::COLORING_WALL_SWITCH) ? $game_switches[Saba::Three_D::COLORING_WALL_SWITCH] : nil
        # The map's animation mode is left out: Dungeon_Sprite never reads it.
        [map.map_id, map.data.__id__,
         player.x, player.y, player.direction, player.rotation_right?, player.rotation_left?, player.move_front?,
         switch, dungeon && dungeon.darkness, dungeon && dungeon.dark_all,
         dungeon && colour.call(dungeon.bg_color), dungeon && colour.call(dungeon.wall_color),
         @bitmaps.__id__, @dark.__id__, @start_ox, @offset_x, @y, @width, @height,
         @character_sprites.__id__, @character_sprites && @character_sprites.size, events]
      end
    end
    true
  end

  def self.capture
    player = $game_player
    raise 'no player' unless player && $game_map
    [PLAYER.map { |v| player.instance_variable_get(v) },
     $game_map.instance_variable_get(:@animation_type)]
  end

  def self.with_state(state)
    player = $game_player
    now = capture
    return yield if now == state
    begin
      PLAYER.each_with_index { |v, i| player.instance_variable_set(v, state[0][i]) }
      $game_map.instance_variable_set(:@animation_type, state[1])
      yield
    ensure
      PLAYER.each_with_index { |v, i| player.instance_variable_set(v, now[0][i]) }
      $game_map.instance_variable_set(:@animation_type, now[1])
    end
  end

  def self.defer(sprite)
    @pending << sprite unless @pending.include?(sprite)
  end

  # Before anything shows or captures the screen.
  def self.flush
    install unless @installed
    unless @pending.empty?
      list = @pending
      @pending = []
      list.each do |sprite|
        begin
          sprite.hardrpg_flush
        rescue StandardError
          # Draw as the game asked, without the bookkeeping.
          sprite.instance_variable_set(:@hardrpg_drawn_key, nil)
          sprite.instance_variable_set(:@hardrpg_window, nil)
          sprite.__hardrpg_refresh_3d rescue nil
        end
      end
    end
    sprite = @clipped
    return unless sprite
    begin
      if sprite.instance_variable_get(:@hardrpg_window)
        return if sprite.hardrpg_window_shown?
        sprite.hardrpg_unclip
      end
    rescue StandardError
      sprite.instance_variable_set(:@hardrpg_window, nil)
      sprite.instance_variable_set(:@hardrpg_drawn_key, nil)
    end
    @clipped = nil
  end
end

if HardRPGDungeonRedraw.uses_script?($RGSS_SCRIPTS)
  class << Graphics
    [:update, :freeze, :transition, :snap_to_bitmap].each do |name|
      next unless method_defined?(name)
      original = :"__hardrpg_dungeon_#{name}"
      alias_method original, name
      define_method(name) do |*args, &block|
        HardRPGDungeonRedraw.flush
        __send__(original, *args, &block)
      end
    end
  end
end

# KGC "Passive Skills" (VX) recalculates an actor's passive bonuses with a
# full Marshal copy of the actor, only so that passive skills with a
# "needs this weapon / armor / state" condition can be checked against the
# copy. The copy is made for every actor on every recalculation, and the
# script recalculates all actors in the database after each load: on the
# Vita that was about 10 s of a Shin Megami Tensei VX load (~900 demons).
# The copy is skipped when nothing can read it: none of the actor's passive
# skills has a condition, and none can change two-weapon style (the copy's
# own recalculation would then unequip an item). The actor then takes the
# path the copy would have taken, which gives the same bonuses.
module HardRPGPassiveSkillFast
  @installed = false
  @tries = 0

  def self.uses_script?(scripts)
    scripts.is_a?(Array) && scripts.any? do |section|
      text = section.is_a?(Array) && section[3]
      text.is_a?(String) && text.include?('def restore_passive_rev') &&
        text.include?('Marshal.load(Marshal.dump(self))') && text.include?('passive_dummy_flag')
    end
  end

  def self.compatible?
    return false unless defined?(::Game_Actor) && defined?(::RPG::Skill)
    actor = ::Game_Actor
    return false unless [:restore_passive_rev, :passive_skill_valid?, :skills].all? { |m| actor.method_defined?(m) }
    return false unless [:passive, :passive_conditions, :passive_effects].all? { |m| ::RPG::Skill.method_defined?(m) }
    # Both from the same script section, so no other script changed the check.
    where = actor.instance_method(:restore_passive_rev).source_location
    check = actor.instance_method(:passive_skill_valid?).source_location
    !where.nil? && !check.nil? && where[0] == check[0]
  end

  def self.copy_unused?(actor)
    effects = actor.instance_variable_get(:@passive_effects)
    return false if effects.is_a?(Hash) && effects[:two_swords_style]
    actor.skills.each do |skill|
      next unless skill.respond_to?(:passive) && skill.passive
      return false if skill.passive_conditions.any? { |_k, v| !(v.respond_to?(:empty?) && v.empty?) }
      return false if skill.passive_effects[:two_swords_style]
    end
    true
  end

  # Tried on the first frames, once the game's scripts are all defined.
  def self.install
    return true if @installed
    return false if @tries > 600
    @tries += 1
    return false unless compatible?
    @installed = true
    ::Game_Actor.class_eval do
      alias_method :__hardrpg_restore_passive_rev, :restore_passive_rev
      def restore_passive_rev
        fast = !@passive_dummy_flag && !@__passive_rev_restoring &&
               (HardRPGPassiveSkillFast.copy_unused?(self) rescue false)
        return __hardrpg_restore_passive_rev unless fast
        had = instance_variable_defined?(:@passive_dummy_flag)
        before = @passive_dummy_flag
        @passive_dummy_flag = true
        begin
          __hardrpg_restore_passive_rev
        ensure
          if had
            @passive_dummy_flag = before
          else
            remove_instance_variable(:@passive_dummy_flag)
          end
        end
      end
    end
    true
  end
end

if HardRPGPassiveSkillFast.uses_script?($RGSS_SCRIPTS)
  class << Graphics
    if method_defined?(:update)
      alias_method :__hardrpg_passive_update, :update
      def update(*args, &block)
        HardRPGPassiveSkillFast.install
        __hardrpg_passive_update(*args, &block)
      end
    end
  end
end

# Syntax that Ruby 1.8/1.9 accepted with a warning and Ruby 3.1 rejects.
# The engine calls repair only after a script section raises SyntaxError,
# before any of that section has run, and evaluates the result in its place,
# so a section that parses is never touched. Each rewrite keeps the line
# count and the 1.8/1.9 meaning, and only touches the lines Ruby reported.
module HardRPGLegacySyntax
  NOT_CALLS = %w[and begin case defined? do else elsif ensure for if in not or
                 rescue return then unless until when while].freeze

  # Returns the repaired source, or nil when nothing reported can be fixed.
  def self.repair(source, message)
    lines = source.split("\n", -1)
    changed = false
    message.scan(/:(\d+): (.*)$/) do |number, text|
      index = number.to_i - 1
      line = lines[index]
      next unless line
      fixed = repair_line(line, text)
      next if fixed.nil? || fixed == line
      lines[index] = fixed
      changed = true
    end
    changed ? lines.join("\n") : nil
  end

  def self.repair_line(line, text)
    if text.include?('else without rescue is useless')
      # 1.9 ran the else body after the begin body; a rescue that re-raises
      # keeps that meaning and makes the clause legal (The Endless Empty).
      line.sub(/(?<![\w.:])else\b/, 'rescue ::Exception; raise; else')
    elsif text.include?("unexpected ':'") && line =~ /\A\s*when\b/
      line.sub(/\A(\s*when\s+[^:'"]*?[^:\s'"])\s*:(\s|\z)/, '\1 then\2') # `when 1: foo`
    elsif text.include?('syntax error')
      join_spaced_call(line) # `Color.new (189, 225, 242)` (VorlorN)
    end
  end

  # Ruby 1.8 read `name (a, b)` as `name(a, b)`. Join only a group holding a
  # top-level comma: `(a, b)` is not an expression, so there is no other reading.
  def self.join_spaced_call(line)
    result = line.dup
    offset = 0
    while (match = /([A-Za-z_]\w*[!?]?)([ \t]+)\(/.match(result, offset))
      open = match.end(0) - 1
      close = matching_paren(result, open)
      break unless close
      if !NOT_CALLS.include?(match[1]) && top_level_comma?(result, open, close)
        result[match.begin(2), match[2].length] = ''
        offset = match.begin(2)
      else
        offset = match.end(0)
      end
    end
    result
  end

  def self.matching_paren(line, open)
    depth = 0
    quote = nil
    index = open
    while index < line.length
      char = line[index]
      if quote
        if char == '\\' then index += 1
        elsif char == quote then quote = nil
        end
      elsif char == '"' || char == "'" then quote = char
      elsif char == '(' then depth += 1
      elsif char == ')'
        depth -= 1
        return index if depth.zero?
      end
      index += 1
    end
    nil
  end

  def self.top_level_comma?(line, open, close)
    depth = 0
    quote = nil
    index = open + 1
    while index < close
      char = line[index]
      if quote
        if char == '\\' then index += 1
        elsif char == quote then quote = nil
        end
      elsif char == '"' || char == "'" then quote = char
      elsif '([{'.include?(char) then depth += 1
      elsif ')]}'.include?(char) then depth -= 1
      elsif char == ',' && depth.zero? then return true
      end
      index += 1
    end
    false
  end
end
