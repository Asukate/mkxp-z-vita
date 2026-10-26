# Native map characters (preload, VX Ace). The engine (binding/native-events.cpp) has native versions
# of the default Game_CharacterBase / Game_Character / Game_Event / Game_Map / Sprite_Base /
# Sprite_Character / Spriteset_Map methods that run for every event every frame. Right after each game
# script section runs, this file looks for default methods in it: a method whose text (comments and
# spacing ignored) is exactly the default one, and which that section really defined, is swapped for
# the native version before any later script can alias it. A game that edited a method keeps its own,
# and the native code calls a game's own version of any method it replaced.
#
# Set $hardrpg_native_events = false in a per-game shim to keep every method in Ruby.
require 'zlib' unless defined?(Zlib)

module HardRPGNative
  # Same normalizer as the corpus tool that produced STOCK.
  module Norm
    module_function

    def strip_comment(line)
      q = nil; esc = false; i = 0
      while i < line.size
        c = line[i]
        if q
          if esc then esc = false
          elsif c == '\\' then esc = true
          elsif c == q then q = nil
          end
        elsif c == '"' || c == "'" then q = c
        elsif c == '#' then return line[0, i]
        end
        i += 1
      end
      line
    end

    def norm_line(line)
      strip_comment(line).strip.gsub(/[ \t]+/, ' ')
    end

    # {"Class#meth" => [normalized text, def line]} for defs indented by one or two spaces (or a tab)
    # in column-0 class bodies; the def ends at an `end` with the same indent.
    def methods(src)
      out = {}; cls = nil; cur = nil; body = nil; start = 0; ind = nil
      src.each_line.with_index(1) do |raw, ln|
        line = raw.chomp
        if cur
          if line.start_with?(ind) && line[ind.size..] =~ /\Aend\s*(#.*)?\z/
            out["#{cls}##{cur}"] = [body.reject(&:empty?).join("\n"), start]; cur = nil
          else
            body << norm_line(line)
          end
        elsif line =~ /\Aclass (\w+)/ then cls = $1
        elsif line =~ /\Aend\b/ then cls = nil
        elsif cls && line =~ /\A( {1,2}|\t)def ([\w?!=]+|\[\]=?)/
          ind = $1; cur = $2; start = ln; body = [norm_line(line)]
        end
      end
      out
    end
  end

  # "Class#method:crc32:adler32:bytes" of the default text => native variant.
  STOCK = {
    "Game_CharacterBase#moving?:1475832270:784076165:42" => "",
    "Game_CharacterBase#jumping?:429998353:2489452997:28" => "",
    "Game_CharacterBase#stopping?:3763998028:3688958947:35" => "",
    "Game_CharacterBase#real_move_speed:577099305:3141996512:49" => "",
    "Game_CharacterBase#distance_per_frame:3179632792:3675001103:51" => "",
    "Game_CharacterBase#dash?:34554339:1505429508:22" => "",
    "Game_CharacterBase#object_character?:2386864988:226235799:66" => "",
    "Game_CharacterBase#shift_y:655568402:46140527:37" => "",
    "Game_CharacterBase#jump_height:2297266646:3235519255:83" => "",
    "Game_CharacterBase#screen_x:2084972125:3339194441:50" => "",
    "Game_CharacterBase#screen_y:3910079482:2928023726:74" => "",
    "Game_CharacterBase#screen_z:1609653884:3514108817:33" => "",
    "Game_CharacterBase#normal_priority?:2033081975:1033703121:40" => "",
    "Game_CharacterBase#update:267784733:2538612962:107" => "",
    "Game_CharacterBase#update_move:2146761426:3471141494:308" => "",
    "Game_CharacterBase#update_stop:2739281036:2653360373:47" => "",
    "Game_CharacterBase#update_animation:3824503162:488844402:124" => "",
    "Game_CharacterBase#update_anime_count:1412043622:3506385190:141" => "",
    "Game_CharacterBase#update_anime_pattern:394819258:1185229588:127" => "",
    "Game_Character#update_stop:4100045483:641997076:64" => "",
    "Game_Event#update:1029356763:3368172214:154" => "",
    "Game_Event#update_stop:242509668:2887785271:69" => "",
    "Game_Event#update_self_movement:3871028437:3894953454:188" => "",
    "Game_Event#near_the_screen?:3015612924:2401320243:203" => "",
    "Game_Event#stop_count_threshold:888440796:3652194748:51" => "",
    "Game_Event#check_event_trigger_auto:4159822488:4253946530:51" => "",
    "Game_Map#adjust_x:139422648:492383510:135" => "",
    "Game_Map#adjust_y:901054100:36580577:135" => "",
    "Game_Map#loop_horizontal?:823879224:1222645701:67" => "",
    "Game_Map#loop_vertical?:2165053105:212014804:65" => "",
    "Game_Map#width:2098289659:1256458054:20" => "",
    "Game_Map#height:2127464467:1504446456:22" => "",
    "Game_Map#screen_tile_x:2798891327:130354449:37" => "",
    "Game_Map#screen_tile_y:3575891027:365432171:38" => "",
    "Game_Map#update_events:3280958066:797714417:104" => "",
    "Sprite_Base#update:1618497493:1556683970:77" => "",
    "Sprite_Base#animation?:417777797:3195210554:32" => "",
    "Sprite_Base#update_animation:1467497474:2024050448:398" => "",
    "Sprite_Character#update:3098554649:2917607848:107" => "",
    "Sprite_Character#update_bitmap:2265517501:2580238685:223" => "",
    "Sprite_Character#graphic_changed?:284539734:2798269989:149" => "",
    "Sprite_Character#update_src_rect:1645443067:3789378685:266" => "",
    "Sprite_Character#update_position:1749224412:285484680:106" => "jp",
    "Sprite_Character#update_position:2227467018:4133764533:171" => "en",
    # Enterbrain's VXAce_SP1 patch wraps the Japanese default (aliased as vxace_sp1_update_position).
    "Sprite_Character#update_position:263073515:4111477022:110" => "sp1",
    "Sprite_Character#update_other:800444980:3104521863:169" => "",
    "Sprite_Character#update_balloon:1060246490:2426233986:300" => "",
    "Sprite_Character#setup_new_effect:2239552987:2721209269:254" => "",
    "Sprite_Character#move_animation:797681945:346371957:164" => "",
    "Spriteset_Map#update_characters:376579292:3236375906:121" => "",
    "Game_Variables#[]:4203845366:1357909825:43" => "",
    # Popular scripts, each matched by its exact text.
    # symbol encounter (シンボルエンカウント補助)
    "Game_Character#update:3213147346:1473975782:55" => "symbol",
    "Game_Character#update_symbol_opacity:4173370896:2147224096:25" => "symbol",
    "Game_Event#update:2022575935:3155700400:116" => "symbol",
    "Game_Event#update_symbol_opacity:2314484639:1617837730:171" => "symbol",
    "Game_Event#update_self_movement:3474066319:288505956:167" => "symbol",
    # CACAO off-screen movement (画面外の自律移動)
    "Game_Event#update_self_movement:1117636545:3755161254:147" => "cacao",
    "Game_Event#self_movable?:74783559:55588901:146" => "cacao",
    # 8-direction movement (星潟)
    "Game_CharacterBase#real_move_speed:2922899733:1822573505:206" => "8dir",
    # kure region pass (リージョン通行設定)
    "Game_CharacterBase#screen_z:1222549236:3044551299:114" => "kure",
    # MOG Anti Lag
    "Game_Event#update:3893618193:2277843059:87" => "mog",
    "Game_Event#update_anti_lag:1428368604:3564578511:103" => "mog",
    "Game_Event#anti_lag_event_on_screen:1273155533:3668997825:349" => "mog",
    "Sprite_Character#check_can_update_sprite:3303875333:3608359256:132" => "mog",
    "Sprite_Character#update:3284189310:3276223833:143" => "mog",
  }.freeze

  # Wrapper variants: "Class#method/variant" => the alias the wrapper calls. The native wrapper
  # calls the native that alias points to, so the alias must be a native (C) method of that name.
  WRAPS = {
    "Sprite_Character#update_position/sp1" => :vxace_sp1_update_position,
    "Game_Character#update/symbol" => :_symbol_opacity_update,
    "Game_Event#update/symbol" => :_update_with_symbol,
    "Game_Event#update_self_movement/symbol" => :_update_self_movement_with_symbol,
    "Game_CharacterBase#real_move_speed/8dir" => :real_move_speed_8direction,
    "Game_Event#update/mog" => :mog_anti_lag_update,
    "Sprite_Character#update/mog" => :mog_anti_lag_update,
  }.freeze

  CLASSES = %w[Game_CharacterBase Game_Character Game_Event Game_Map Game_Variables Sprite_Base Sprite_Character
               Spriteset_Map].freeze
  ATTRS = { 'Game_CharacterBase' => %i[tile_id character_name character_index pattern direction opacity blend_type
                                       bush_depth transparent animation_id balloon_id],
            'Game_Map' => %i[display_x display_y],
            'Game_Event' => %i[can_update],
            'Game_System' => %i[anti_lag] }.freeze
  @swapped = []
  @kept = []

  class << self
    attr_reader :swapped, :kept

    def status
      return 'off (disabled)' if $hardrpg_native_events == false
      "on (#{@swapped.size} native, #{@kept.size} kept)"
    end

    def section_loaded(fname, src)
      return if $hardrpg_native_events == false
      return unless (CLASSES + ATTRS.keys).any? { |c| src.include?("class #{c}") }
      Norm.methods(src).each do |key, (text, line)|
        cname, name = key.split('#', 2)
        next unless CLASSES.include?(cname)
        variant = STOCK["#{key}:#{Zlib.crc32(text)}:#{Zlib.adler32(text)}:#{text.bytesize}"]
        klass = Object.const_defined?(cname) ? Object.const_get(cname) : nil
        next unless klass.is_a?(Class)
        um = (klass.instance_method(name) rescue nil)
        # The section must be what defined the method now in place, at that line.
        defined_here = um && um.owner.equal?(klass) && um.source_location == [fname.to_s, line]
        # A wrapper calls the method it aliased: native only when that alias is still a native
        # method of the same name (SP1 also needs the Japanese default under it).
        if variant && (al = WRAPS["#{key}/#{variant}"])
          am = (klass.instance_method(al) rescue nil)
          variant = nil unless am && am.source_location.nil? && am.original_name == name.to_sym
          variant = nil if variant == 'sp1' && (@variant_of ||= {})[key] != 'jp'
        end
        if variant && defined_here && klass.public_method_defined?(name, false)
          if replace(klass, name.to_sym, variant)
            @swapped << key
            (@variant_of ||= {})[key] = variant
          else
            @kept << "#{key} (no native)"
          end
        elsif STOCK.keys.any? { |k| k.start_with?("#{key}:") }
          @kept << key # a method we know, but this game's text differs
        end
      end
      lines = src.lines
      ATTRS.each do |cname, names|
        next unless src.include?("class #{cname}") && Object.const_defined?(cname)
        klass = Object.const_get(cname)
        names.each do |n|
          um = (klass.instance_method(n) rescue nil)
          loc = um && um.owner.equal?(klass) && um.source_location
          next unless loc && loc[0] == fname.to_s && lines[loc[1] - 1].to_s =~ /\A\s*attr_(reader|accessor)\s.*:#{n}\b/
          attr(klass, n)
        end
      end
    end
  end
end
