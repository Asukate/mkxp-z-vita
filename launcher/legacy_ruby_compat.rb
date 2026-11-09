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

# Ruby 1.8 gave every object id, an old name for object_id; 1.9 removed it.
# Exit Fate's party screen reads the id of all six party slots, empty ones
# (nil) too, so it quit with NoMethodError whenever the party had fewer than
# six members (issue 15). Finding Paradise starts with an empty party and
# its caterpillar script reads $game_party.actors[0].id (issue 17). nil and
# true answer what Ruby 1.8 gave them (4 and 2; false and Integers already
# match), so scripts that keep these ids see the same values as on Windows. Classes with their own id (actors, items,
# events) keep theirs. RGSS1/2 only, like to_a.
unless Kernel.respond_to?(:rgss_main, true) || Object.method_defined?(:id)
  class Object
    def id
      object_id
    end
  end

  class NilClass
    def id
      4
    end
  end

  class TrueClass
    def id
      2
    end
  end
end

# In Ruby 1.8 `proc { }` made a lambda, so a `return` in it only left the
# block. Ruby 3.1 raises LocalJumpError once the method that made the block
# has returned: Essentials event handlers such as Pokemon Uranium's surf
# prompt (Events.onAction, `return if $PokemonGlobal.surfing`) ended the game
# with "unexpected return" whenever the player pressed confirm on the map.
# Calling such a block answers that return as the block's value, as 1.8 did.
# RGSS1/2 only, like to_a.
unless Kernel.respond_to?(:rgss_main, true) || defined?(HardRPGProcReturn)
  module HardRPGProcReturn
    def call(*args, &block)
      super
    rescue LocalJumpError => e
      raise unless e.reason == :return
      e.exit_value
    end

    def [](*args, &block)
      super
    rescue LocalJumpError => e
      raise unless e.reason == :return
      e.exit_value
    end
  end
  Proc.prepend(HardRPGProcReturn)
end

# Other Ruby 1.8 methods that 1.9 removed, for RGSS1/2 games (VX Ace already
# ran on 1.9). Each is added only when nothing defines it, so a class or
# script with its own keeps it.
unless Kernel.respond_to?(:rgss_main, true)
  # Object#type, the old name for class. Rataime's "Sprite Destruction"
  # (VorlorN) checks `check.type == Fixnum` when the player faces an event.
  class Object
    def type
      self.class
    end unless method_defined?(:type)
  end

  # String was Enumerable by lines: `text.each { |line| ... }`.
  class String
    def each(separator = $/, &block)
      return each_line(separator) unless block
      each_line(separator, &block)
      self
    end unless method_defined?(:each)
  end

  class Array
    # Number of elements that are not nil (Pokemon Essentials defines its own).
    def nitems
      count { |item| !item.nil? }
    end unless method_defined?(:nitems)

    # Elements at the given indexes, as values_at.
    def indexes(*indexes)
      values_at(*indexes)
    end unless method_defined?(:indexes)

    alias_method :indices, :indexes unless method_defined?(:indices)
  end

  class Hash
    # Values for the given keys, as values_at.
    def indexes(*keys)
      values_at(*keys)
    end unless method_defined?(:indexes)

    alias_method :indices, :indexes unless method_defined?(:indices)
  end
end

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
    # Matched as bytes: an RGSS1 section may not be valid UTF-8 (Pokemon Uranium).
    return nil unless source.include?('$!') && source.b =~ CLEAR
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

# Ruby 1.8/1.9 read a class variable inside a top-level `class << Graphics`
# as Object's class variable, with a warning; Ruby 3.0 raises "class variable
# access from toplevel". CACAO's Screenshot script does it, so A New Game
# (RJ273807) stopped before its title screen (issue 13). Wrapping the block in
# `class Object; ... end` on the same lines gives Object's class variable, as
# 1.9 did. Only blocks on a named class or module are wrapped: on `self`
# the wrap would change what self is. Ruby's own parser finds the block.
module HardRPGToplevelClassVars
  OPEN = 'class Object; '.freeze
  CLOSE = '; end'.freeze
  SKIP = %i[CLASS MODULE DEFN DEFS].freeze
  RECEIVERS = %i[CONST COLON2 COLON3].freeze

  def self.rewrite(source)
    return nil unless source.include?('@@') && source.include?('class <<')
    root = begin
      RubyVM::AbstractSyntaxTree.parse(source)
    rescue SyntaxError, StandardError
      return nil
    end
    blocks = []
    collect(root, blocks)
    return nil if blocks.empty?
    lines = source.split("\n", -1).map { |line| line.dup.force_encoding(Encoding::BINARY) }
    # Last block first, so earlier positions still hold.
    blocks.sort_by { |node| [-node.first_lineno, -node.first_column] }.each do |node|
      lines[node.last_lineno - 1].insert(node.last_column, CLOSE)
      lines[node.first_lineno - 1].insert(node.first_column, OPEN)
    end
    lines.join("\n").force_encoding(source.encoding)
  end

  # Top-level `class << Name` blocks holding a class variable. Class, module
  # and method bodies are not top level, so their contents are left alone.
  def self.collect(node, found)
    return unless node.is_a?(RubyVM::AbstractSyntaxTree::Node)
    return if SKIP.include?(node.type)
    if node.type == :SCLASS
      found << node if RECEIVERS.include?(node.children[0]&.type) && class_var?(node)
      return
    end
    node.children.each { |child| collect(child, found) }
  end

  def self.class_var?(node)
    return false unless node.is_a?(RubyVM::AbstractSyntaxTree::Node)
    return true if %i[CVAR CVASGN].include?(node.type)
    node.children.any? { |child| class_var?(child) }
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
HardRPGToplevelClassVars.apply($RGSS_SCRIPTS)

# Ruby 1.8 (RGSS1/2) read `name (x).width` as `name(x).width`: a space
# before the parentheses still made them the argument list. Ruby 1.9 and
# later read `name((x).width)`. Finding Paradise's paragraph formatter wraps
# text with `text_size (substring).width > @max_width`, and its menu closed
# the game with NoMethodError (width for a String). The space is removed
# where Ruby's parser shows a call whose single argument starts with that
# parenthesis, closed before a `.` or `[`. Sections that only parse after
# the legacy syntax repair are rewritten there (HardRPGLegacySyntax.repair).
module HardRPGSpacedCall
  CALLS = %i[CALL FCALL QCALL].freeze
  HINT = /[\w?!] +\([^()\n]*\)[.\[]/

  def self.enabled?
    !Kernel.respond_to?(:rgss_main, true)
  end

  def self.rewrite(source)
    # Matched as bytes: an RGSS1 section may not be valid UTF-8 (Pokemon Uranium),
    # and a regexp raises ArgumentError on such text.
    return nil unless source.is_a?(String) && HINT.match?(source.b)
    root = begin
      RubyVM::AbstractSyntaxTree.parse(source)
    rescue SyntaxError, StandardError
      return nil
    end
    bin = source.dup.force_encoding(Encoding::BINARY)
    starts = [0]
    bin.each_line { |line| starts << starts[-1] + line.bytesize }
    cuts = []
    walk(root) do |node|
      next unless CALLS.include?(node.type)
      args = node.children[-1]
      next unless args.is_a?(RubyVM::AbstractSyntaxTree::Node) && args.type == :LIST
      items = args.children.compact
      next unless items.size == 1
      arg = items[0]
      from = starts[node.first_lineno - 1] + node.first_column
      to = starts[arg.first_lineno - 1] + arg.first_column
      space = /[\w?!]( +)\z/.match(bin[from...to])
      next unless space && bin[to] == '('
      close = matching(bin, to)
      next unless close && close < starts[arg.last_lineno - 1] + arg.last_column - 1
      next unless bin[close + 1] == '.' || bin[close + 1] == '['
      cuts << [to - space[1].size, space[1].size]
    end
    return nil if cuts.empty?
    cuts.uniq.sort.reverse_each { |at, size| bin[at, size] = '' }
    bin.force_encoding(source.encoding)
  end

  def self.walk(node, &block)
    return unless node.is_a?(RubyVM::AbstractSyntaxTree::Node)
    yield node
    node.children.each { |child| walk(child, &block) }
  end

  # The ")" closing the "(" at open (quoted text skipped), or nil.
  def self.matching(bin, open)
    depth = 0
    i = open
    while i < bin.size
      c = bin[i]
      if c == '"' || c == "'"
        i += 1
        i += (bin[i] == '\\' ? 2 : 1) while i < bin.size && bin[i] != c
      elsif c == '('
        depth += 1
      elsif c == ')'
        depth -= 1
        return i if depth.zero?
      elsif c == "\n"
        return nil
      end
      i += 1
    end
    nil
  end

  def self.apply(scripts)
    return unless enabled? && scripts.is_a?(Array)
    scripts.each do |section|
      next unless section.is_a?(Array) && section[3].is_a?(String)
      fixed = rewrite(section[3])
      section[3] = fixed if fixed
    end
  end
end
HardRPGSpacedCall.apply($RGSS_SCRIPTS)

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
# count and the 1.8/1.9 meaning, and only touches the construct Ruby reported.
module HardRPGLegacySyntax
  NOT_CALLS = %w[and begin case defined? do else elsif ensure for if in not or
                 rescue return then unless until when while].freeze

  # Returns the repaired source, or nil when nothing reported can be fixed.
  def self.repair(source, message)
    fixed = repair_syntax(source, message)
    return fixed unless fixed && HardRPGSpacedCall.enabled?
    HardRPGSpacedCall.rewrite(fixed) || fixed
  end

  def self.repair_syntax(source, message)
    if message.include?('expecting =>')
      message.scan(/:(\d+): .*expecting =>/) do |number|
        fixed = string_pair_hash(source, number[0].to_i)
        return fixed if fixed
      end
    end
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

  # Ruby 1.8 read {'key', 'value'} as {'key' => 'value'}. Only repair a
  # reported assignment whose whole hash consists of quoted string pairs;
  # quotes, escaped characters, comments and multiline values are preserved.
  # Other expressions and already explicit hashes keep their original error.
  def self.string_pair_hash(source, number)
    lines = source.lines
    line = lines[number - 1]
    return unless line
    start = /\A\s*[@$]?[A-Za-z_]\w*\s*=\s*(\{)/.match(line)
    return unless start
    offset = lines.take(number - 1).sum(&:length) + start.end(1)
    cursor = offset
    commas = []
    count = 0
    loop do
      space = /\G(?:\s|\#[^\n]*)*/.match(source, cursor)
      cursor = space.end(0)
      token = /\G(?:'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*")/m.match(source, cursor)
      return unless token
      count += 1
      cursor = token.end(0)
      cursor = /\G(?:\s|\#[^\n]*)*/.match(source, cursor).end(0)
      case source[cursor]
      when ','
        commas << cursor if count.odd?
        cursor += 1
        cursor = /\G(?:\s|\#[^\n]*)*/.match(source, cursor).end(0)
        break if source[cursor] == '}'
      when '}' then break
      else return
      end
    end
    return if count.odd? || commas.empty?
    fixed = source.dup
    commas.reverse_each { |index| fixed[index] = ' =>' }
    fixed
  end

  def self.repair_line(line, text)
    if text.include?('else without rescue is useless')
      # 1.9 ran the else body after the begin body; a rescue that re-raises
      # keeps that meaning and makes the clause legal (The Endless Empty).
      line.sub(/(?<![\w.:])else\b/, 'rescue ::Exception; raise; else')
    elsif text.include?('Invalid break')
      # 1.8 compiled a break outside any loop or block and raised
      # LocalJumpError if it ran (Insurgence's notebook and online menus).
      line.sub(/(?<![\w.:@$])break\b/, "raise(LocalJumpError, 'unexpected break')")
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

# Ruby 1.8 had no string encodings: a regexp matched bytes, and RGSS1 games
# (every Pokemon Essentials release up to v17 among them) spell UTF-8
# characters as byte escapes, /\xe3\x81[\x81-\xbf]/n. Ruby 3.1 matches
# characters: such a regexp fails to parse ("invalid multibyte escape"), or
# parses and raises Encoding::CompatibilityError on the first non-ASCII text
# it meets. Each byte sequence that spells UTF-8 characters becomes the same
# characters (a class of code point ranges), so the regexp matches what it
# matched in 1.8; other high bytes are read as Latin-1 code points.
module HardRPGLegacyRegexp
  HIGH = /\\x[89a-fA-F]\h/
  # /body/flags where an operand starts; division never holds a \x escape.
  LITERAL = /(?:^|(?<=[\s(,=!~|&{\[;?:]))\/((?:\\.|\[(?:\\.|[^\]\\\n])*\]|[^\/\\\n\[])+)\/([imxnoues]*)/
  MAX_CODEPOINTS = 300_000

  # Returns the source with its byte regexps converted, or nil when none.
  # An interpolating byte regexp (/#{...}/n) somewhere in the text.
  INTERP_N = /#\{[^\n]*\/[imxoues]*n[imxoues]*(?![\w])/

  def self.prepare(source)
    bytes = source.include?('\\x') && source.b.match?(HIGH)
    interp = source.include?('#{') && source.b.match?(INTERP_N)
    return nil unless bytes || interp
    text = source.b
    changed = false
    if bytes
      text = text.gsub(LITERAL) do |whole|
        body, flags = $1, $2
        next whole unless body.match?(HIGH)
        converted = convert(body)
        next whole unless converted
        changed = true
        "/#{converted}/#{flags.delete('n')}"
      end
    end
    # Regexps interpolate the converted ones, in the same section or another (CJK = /(#{HanZi}|
    # #{Kana})/n, DrawText's /...#{CJKUtils::WordRegexp::WordRegexp}.../n): a converted regexp
    # holds characters, which Ruby 3 refuses inside a byte regexp.
    text = text.gsub(LITERAL) do |w|
      next w unless $2.include?('n') && $1.include?('#{')
      changed = true
      "/#{$1}/#{$2.delete('n')}"
    end
    return nil unless changed
    text.force_encoding(Encoding::UTF_8)
  end

  # Regexp source with byte escapes -> character source, or nil.
  def self.convert(body)
    tokens = tokenize(distribute(body))
    return nil unless tokens
    out = +''
    i = 0
    while i < tokens.size
      kind, value = tokens[i]
      if (kind == :byte || kind == :bclass) && (run = utf8_run(tokens, i))
        ranges, length = run
        out << char_class(ranges)
        i += length
        next
      end
      out << case kind
             when :byte then format('\\u{%X}', value)
             when :bclass then latin1_class(value)
             else value
             end
      i += 1
    end
    Regexp.new(out.dup.force_encoding(Encoding::UTF_8))
    out
  rescue RegexpError, ArgumentError
    nil
  end

  ATOM = /\\x\h\h|\[(?:\\.|[^\]\\])*\]/
  # Byte atoms before a group belong to each alternative: \xef(?:\xbc.|\xbd.)
  # spells the same characters as (?:\xef\xbc.|\xef\xbd.), which decodes.
  def self.distribute(body)
    offset = 0
    while (match = /((?:#{ATOM})+)\(\?:/.match(body, offset))
      offset = match.end(0)
      next unless match[1].match?(HIGH)
      open = match.end(0) - 3
      close = group_end(body, open)
      next unless close && !'*+?{'.include?(body[close + 1].to_s)
      alternatives = split_top(body[(open + 3)...close])
      grouped = '(?:' + alternatives.map { |alt| match[1] + alt }.join('|') + ')'
      body = body[0...match.begin(1)] + grouped + body[(close + 1)..]
      offset = match.begin(1)
    end
    body
  end

  def self.group_end(body, open)
    depth = 0
    i = open
    while i < body.size
      case body[i]
      when '\\' then i += 1
      when '[' then i = class_end(body, i) || (return nil)
      when '(' then depth += 1
      when ')'
        depth -= 1
        return i if depth.zero?
      end
      i += 1
    end
    nil
  end

  def self.split_top(text)
    parts = [+'']
    depth = 0
    i = 0
    while i < text.size
      c = text[i]
      if c == '\\'
        parts[-1] << text[i, 2]
        i += 2
        next
      elsif c == '['
        close = class_end(text, i)
        parts[-1] << text[i..close]
        i = close + 1
        next
      end
      depth += 1 if c == '('
      depth -= 1 if c == ')'
      if c == '|' && depth.zero? then parts << +''
      else parts[-1] << c
      end
      i += 1
    end
    parts
  end

  def self.tokenize(body)
    tokens = []
    i = 0
    while i < body.size
      c = body[i]
      if c == '\\'
        if body[i + 1] == 'x' && body[i + 2, 2] =~ /\A\h\h\z/
          tokens << [:byte, body[i + 2, 2].hex]
          i += 4
        else
          tokens << [:raw, body[i, 2]]
          i += 2
        end
      elsif c == '['
        close = class_end(body, i)
        return nil unless close
        text = body[i..close]
        tokens << (text.match?(HIGH) ? [:bclass, text] : [:raw, text])
        i = close + 1
      elsif c == '#' && body[i + 1] == '{'
        close = body.index('}', i)
        return nil unless close
        tokens << [:raw, body[i..close]]
        i = close + 1
      elsif '*+?{'.include?(c)
        tokens << [:quant, c]
        i += 1
      else
        tokens << [:raw, c]
        i += 1
      end
    end
    tokens
  end

  def self.class_end(body, open)
    i = open + 1
    i += 1 if body[i] == '^'
    i += 1 if body[i] == ']'
    while i < body.size
      return i if body[i] == ']'
      return nil if body[i] == '['
      i += body[i] == '\\' ? 2 : 1
    end
    nil
  end

  # Byte values a token can match, or nil (negated, nested or not all bytes).
  def self.byte_set(token)
    kind, value = token
    return [value] if kind == :byte
    return nil unless kind == :bclass
    inner = value[1...-1]
    return nil if inner.start_with?('^')
    items = inner.scan(/\\x(\h\h)|\\(.)|(.)/m).map { |h, e, ch| h ? h.hex : (e ? nil : ch.ord) }
    return nil if items.include?(nil) || items.any? { |b| b > 0xFF }
    set = []
    j = 0
    while j < items.size
      if inner_range?(inner, items, j)
        set.concat((items[j]..items[j + 2]).to_a)
        j += 3
      else
        set << items[j]
        j += 1
      end
    end
    set.uniq
  end

  # items[j+1] is a '-' joining two members (not an escaped \-).
  def self.inner_range?(_inner, items, j)
    items[j + 1] == '-'.ord && j + 2 < items.size
  end

  def self.utf8_run(tokens, start)
    lead = byte_set(tokens[start])
    return nil unless lead && !lead.empty?
    lengths = lead.map { |b| b >= 0xF0 && b <= 0xF4 ? 4 : (b >= 0xE0 ? 3 : (b >= 0xC2 && b <= 0xDF ? 2 : 0)) }.uniq
    return nil unless lengths.size == 1 && lengths[0] > 0
    length = lengths[0]
    sets = [lead]
    (1...length).each do |k|
      token = tokens[start + k]
      return nil unless token && (set = byte_set(token)) && !set.empty? && set.all? { |b| b >= 0x80 && b <= 0xBF }
      sets << set
    end
    # A quantifier after an inner byte stops the run above (it is not a byte
    # token); one after the last byte now repeats the character.
    count = sets.map(&:size).reduce(:*)
    return nil if count > MAX_CODEPOINTS
    codes = sets[0].product(*sets[1..]).map { |bytes| bytes.pack('C*').force_encoding(Encoding::UTF_8) }
    return nil unless codes.all?(&:valid_encoding?)
    points = codes.map(&:ord).sort
    ranges = []
    points.each { |p| ranges.last && ranges.last[1] + 1 == p ? ranges.last[1] = p : ranges << [p, p] }
    [ranges, length]
  end

  def self.char_class(ranges)
    return format('\\u{%X}', ranges[0][0]) if ranges.size == 1 && ranges[0][0] == ranges[0][1]
    '[' + ranges.map { |a, b| a == b ? format('\\u{%X}', a) : format('\\u{%X}-\\u{%X}', a, b) }.join + ']'
  end

  def self.latin1_class(text)
    text.gsub(/\\x(\h\h)/) { $1.hex >= 0x80 ? format('\\u{%X}', $1.hex) : "\\x#{$1}" }
  end
end
