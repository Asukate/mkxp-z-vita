# Vita-specific fixes for game scripts with unnecessarily expensive redraws.

module VitaTextGlyphCache
  MAX_ENTRIES = 512

  class << self
    def fetch(character, font, height, width)
      @glyphs ||= {}
      key = [
        character, height, font.name, font.size, font.bold, font.italic,
        font.outline, font.shadow, font.color.to_s, font.out_color.to_s
      ]
      return @glyphs[key] if @glyphs.key?(key)

      if @glyphs.length >= MAX_ENTRIES
        _old_key, old_entry = @glyphs.shift
        old_entry[1].dispose unless old_entry[1].disposed?
      end

      bitmap = Bitmap.new([width * 2, 1].max, [height, 1].max)
      bitmap.font = font
      bitmap.draw_text(0, 0, bitmap.width, bitmap.height, character)
      @glyphs[key] = [width, bitmap]
    end
  end
end

# process_normal_character with a position Hash is VX Ace (RGSS3) API;
# RPG Maker VX (RGSS2) lacks it, so skip cleanly there instead of
# raising NameError at preload.
if defined?(Window_Base) && defined?(Bitmap)
  Window_Base.class_eval do
    if method_defined?(:process_normal_character) &&
       !method_defined?(:vita_uncached_process_normal_character)
      alias_method :vita_uncached_process_normal_character, :process_normal_character

      def process_normal_character(character, position)
        width = text_size(character).width
        unless character == " "
          _cached_width, glyph = VitaTextGlyphCache.fetch(
            character, contents.font, position[:height], width
          )
          contents.blt(position[:x], position[:y], glyph, glyph.rect)
        end
        position[:x] += width
      end
    end
  end
end

if defined?(Window_Var) && defined?(Prico::Var_Num)
  Window_Var.class_eval do
    unless method_defined?(:vita_original_refresh)
      alias_method :vita_original_refresh, :refresh

      def refresh
        result = vita_original_refresh
        @vita_last_variable_value = $game_variables[Prico::Var_Num]
        result
      end

      def update
        super
        refresh unless @vita_last_variable_value == $game_variables[Prico::Var_Num]
      end
    end
  end
end
