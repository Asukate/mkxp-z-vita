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

    # The engine's C version of norm_line (binding/native-events.cpp) gives the same text much faster.
    NATIVE = HardRPGNative.respond_to?(:norm_line)

    def strip_comment(line)
      return line unless (hash = line.index('#'))
      return line[0, hash] unless line.include?('"') || line.include?("'")
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
      return HardRPGNative.norm_line(line) if NATIVE
      strip_comment(line).strip.gsub(/[ \t]+/, ' ')
    end

    # {"Class#meth" => [normalized text, def line]} for defs indented by one or two spaces (or a tab)
    # more than their class line; the def ends at an `end` with the same indent. Classes may be
    # indented (a whole script shifted right); the swap still needs the def to be the method in place.
    def methods(src)
      out = {}; stack = []; cur = nil; body = nil; start = 0; ind = nil; single = false
      src.each_line.with_index(1) do |raw, ln|
        line = raw.chomp
        if cur
          if line.start_with?(ind) && line[ind.size..] =~ /\Aend\s*(#.*)?\z/
            out["#{stack.last[0]}##{cur}"] = [body.reject(&:empty?).join("\n"), start]; cur = nil
            (stack.pop; single = false) if single
          else
            body << norm_line(line)
          end
        elsif line =~ /\A(\s*)class << self\s*(#.*)?\z/ && !stack.empty?
          # singleton methods of the enclosing class/module: "Input.self#update"
          ci = $1; stack << ["#{stack.last[0].sub(/\.self\z/, '')}.self", ci]
        elsif line =~ /\A(\s*)(?:class|module) (\w+)/ then stack << [$2, $1]
        elsif !stack.empty? && line =~ /\A(\s*)end\b/ && $1 == stack.last[1] then stack.pop
        elsif !stack.empty? && line.start_with?(stack.last[1]) &&
              line[stack.last[1].size..] =~ /\A( {1,2}|\t)def (self\.)?([\w?!=]+|\[\]=?)/
          if $2
            next if stack.last[0].end_with?('.self')
            stack << ["#{stack.last[0]}.self", stack.last[1]]; single = true
          end
          ind = stack.last[1] + $1; cur = $3; start = ln; body = [norm_line(line)]
        end
      end
      out
    end
  end

  # Khas redraws an opaque ambient bitmap and then draws its static light
  # sources in order. Match the two known sections and check that a later
  # script did not replace the methods before enabling the GPU compositor.
  module KhasLights
    SECTIONS = {
      28672 => [2657479429, 1037494418, [[:Spriteset_Map, :dispose_lights, 931]]],
      3088 => [949117487, 2832299771, [[:Spriteset_Map, :update_lights, 115],
        [:Game_Character, :draw_light, 67], [:Game_Event, :draw_light, 99],
        [:Light_SSource, :draw, 9]]]
    }.freeze
    # Moving lights and the lantern (script 144's Light_DSource#draw): a fresh copy of the light
    # with its shadows cut, blitted over the static lights. Checked separately so static lights
    # keep the GPU path when these differ.
    DYNAMIC = {
      28672 => [[:Light_DSource, :get_graphic, 352], [:Light_DSource, :sx, 377], [:Light_DSource, :sy, 380],
        [:Light_DSource, :x, 383], [:Light_DSource, :y, 386], [:Light_SSource, :opacity, 292],
        [:Light_SSource, :range, 258]],
      3088 => [[:Light_DSource, :draw, 28], [:Light_DSource, :set_shadow, 24]]
    }.freeze

    def self.observe(fname, source)
      return unless defined?(Bitmap) && Bitmap.method_defined?(:__hardrpg_compose_opaque)
      match = SECTIONS[source.bytesize]
      return unless match && Zlib.crc32(source) == match[0] && Zlib.adler32(source) == match[1]
      @sections ||= {}
      found = lambda do |list|
        list.map do |klass, name, line|
          method = (Object.const_get(klass).instance_method(name) rescue nil)
          method if method && method.source_location == [fname, line]
        end
      end
      @sections[source.bytesize] = found.(match[2])
      (@dynamic ||= {})[source.bytesize] = found.(DYNAMIC[source.bytesize])
    end

    class << self
      attr_reader :dynamic_ok
    end

    def self.install
      return if @installed || !@sections || @sections.size != SECTIONS.size
      return unless @sections.values.flatten.all? do |method|
        method && method.owner.instance_method(method.name) == method
      end
      # Every one of these runs on a Light_DSource: what it resolves to there must be the checked one.
      current = lambda do |method|
        now = method && (Light_DSource.instance_method(method.name) rescue nil)
        now && now.owner.equal?(method.owner) && now.source_location == method.source_location
      end
      @dynamic_ok = @dynamic && @dynamic.size == DYNAMIC.size &&
                    @dynamic.values.flatten(1).all?(&current) ? true : false
      Spriteset_Map.prepend(Updates)
      @installed = true
    end

    module Updates
      def update_lights
        surface = $game_map.light_surface
        if surface
          lights = $game_map.light_sources
          dynamic = KhasLights.dynamic_ok
          lantern = $game_map.lantern
          lantern = nil unless lantern.visible
          if $hardrpg_gpu_lights != false && (!lantern || (dynamic && lantern.instance_of?(Light_DSource))) &&
             (!lights.empty? || lantern) && lights.size <= 64 && surface.bitmap.width == Graphics.width &&
             surface.bitmap.height == Graphics.height &&
             lights.all? { |event| event.instance_of?(Game_Event) &&
               ((light = event.instance_variable_get(:@light)).instance_of?(Light_SSource) ||
                (dynamic && light.instance_of?(Light_DSource))) }
            ambient = $game_map.effect_surface.color
            if ambient.alpha == 255 && compose_static_lights(surface, lights, ambient, lantern)
              return
            end
          end
        end
        dispose_light_copies
        restore_cpu_lights(surface)
        super
      end

      def dispose_lights
        dispose_light_copies
        restore_cpu_lights($game_map.light_surface)
        @hardrpg_light_gpu.dispose if @hardrpg_light_gpu && !@hardrpg_light_gpu.disposed?
        @hardrpg_light_rows = @hardrpg_light_gpu = @hardrpg_light_cpu = nil
        super
      end

      private

      def restore_cpu_lights(surface)
        if surface && @hardrpg_light_cpu && surface.bitmap.equal?(@hardrpg_light_gpu)
          surface.bitmap = @hardrpg_light_cpu
        end
      end

      # The copies a moving light draws are kept one frame (the script disposes each right after
      # its blt), so they can still be sampled when the frame is composed.
      def dispose_light_copies
        copies = @hardrpg_light_copies
        return unless copies && !copies.empty?
        copies.each { |bitmap| bitmap.dispose unless bitmap.disposed? }
        copies.clear
      end

      # Light_DSource#draw (script 144) up to its blt. A light whose blt lands wholly off the
      # surface draws nothing: its copy and shadows are skipped (get_graphic, visible?, within? only
      # read; render_shadow only cuts that copy and leaves scratch values its next call recomputes).
      # The reads, and opacity (the flicker draws a random number), happen as in the script.
      # False when the light draws nothing.
      def dynamic_light_row(light, row)
        x = light.x
        y = light.y
        r = light.range
        sx = x + r
        sy = y + r
        dr = r * 2
        bx = light.sx.to_i
        by = light.sy.to_i
        if bx >= Graphics.width || by >= Graphics.height || bx + dr <= 0 || by + dr <= 0
          light.opacity
          return false
        end
        btr = light.get_graphic
        @hardrpg_light_copies << btr
        unless light.instance_variable_get(:@skip_shadow)
          $game_map.surfaces.each do |s|
            s.render_shadow(x, y, sx, sy, r, btr) if s.visible?(sx, sy) && s.within?(x, x + dr, y, y + dr)
          end
        end
        row[0] = btr
        row[1] = bx
        row[2] = by
        row[3] = dr
        row[4] = dr
        row[5] = light.opacity
        true
      end

      def compose_static_lights(surface, lights, ambient, lantern = nil)
        return false if @hardrpg_light_unavailable
        unless @hardrpg_light_gpu
          begin
            @hardrpg_light_gpu = Bitmap.new(Graphics.width, Graphics.height)
          rescue MKXPError, SDLError
            @hardrpg_light_unavailable = true
            return false
          end
        end
        return false if @hardrpg_light_gpu.disposed? || @hardrpg_light_gpu.width != Graphics.width ||
                        @hardrpg_light_gpu.height != Graphics.height

        dispose_light_copies
        @hardrpg_light_copies ||= []
        count = lights.size + (lantern ? 1 : 0)
        rows = @hardrpg_light_rows
        rows = @hardrpg_light_rows = Array.new(count) { Array.new(6) } if !rows || rows.size != count
        n = 0
        lights.each do |event|
          light = event.instance_variable_get(:@light)
          row = rows[n]
          unless light.instance_of?(Light_SSource)
            n += 1 if dynamic_light_row(light, row)
            next
          end
          n += 1
          row[0] = light.bitmap
          row[1] = light.sx.to_i
          row[2] = light.sy.to_i
          row[3] = light.w
          row[4] = light.h
          row[5] = light.opacity
        end
        n += 1 if lantern && dynamic_light_row(lantern, rows[n])
        rows = rows.first(n) if n < rows.size
        @hardrpg_light_drawn = rows
        if @hardrpg_light_gpu.__hardrpg_compose_opaque(ambient, rows)
          unless surface.bitmap.equal?(@hardrpg_light_gpu)
            @hardrpg_light_cpu = surface.bitmap
            surface.bitmap = @hardrpg_light_gpu
          end
        else
          # Replay the already sampled opacities. Calling the original
          # method now would advance the game's flicker RNG a second time.
          restore_cpu_lights(surface)
          target = surface.bitmap
          target.clear
          target.fill_rect(0, 0, Graphics.width, Graphics.height, ambient)
          rows.each { |bitmap, x, y, w, h, opacity| target.blt(x, y, bitmap, Rect.new(0, 0, w, h), opacity) }
        end
        true
      end
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
    # Passability: passable? and every default method it runs (steps and pathfinding searches).
    "Game_CharacterBase#passable?:2890133910:1550089906:359" => "",
    "Game_CharacterBase#map_passable?:881579081:234099216:55" => "",
    "Game_CharacterBase#debug_through?:1922958208:3150056395:31" => "",
    "Game_CharacterBase#reverse_dir:1090973604:3196455579:32" => "",
    "Game_CharacterBase#collide_with_characters?:2823222209:1508647427:93" => "",
    "Game_CharacterBase#collide_with_events?:1333215762:3355127436:128" => "",
    "Game_CharacterBase#collide_with_vehicles?:746340996:524755088:93" => "",
    "Game_CharacterBase#pos?:1023083197:2722236635:33" => "",
    "Game_CharacterBase#pos_nt?:1977972506:296422599:41" => "",
    "Game_Vehicle#pos?:3553531591:248844825:57" => "",
    "Game_Event#collide_with_characters?:3997802833:3825278515:81" => "",
    "Game_Event#collide_with_player_characters?:82831582:3745194047:89" => "",
    "Game_Player#map_passable?:2140084804:423311533:163" => "",
    "Game_Player#debug_through?:1654466854:2173046618:47" => "",
    "Game_Player#collide?:1958870256:1368725325:72" => "",
    "Game_Map#passable?:516777856:3980661519:69" => "",
    "Game_Map#check_passage:973991880:1095123498:199" => "",
    "Game_Map#all_tiles:2025312439:2612010914:89" => "",
    "Game_Map#layered_tiles:3802473541:3432518973:65" => "",
    "Game_Map#tile_id:367210931:1138232817:44" => "",
    "Game_Map#tile_events_xy:937110794:678566796:75" => "",
    "Game_Map#tileset:3363835930:610275068:39" => "",
    "Game_Map#events_xy_nt:2271787243:574692209:75" => "",
    "Game_Map#valid?:4029894196:4133621796:60" => "",
    "Game_Map#round_x_with_direction:3260917369:3740472808:76" => "",
    "Game_Map#round_y_with_direction:3040158680:3749320172:76" => "",
    "Game_Map#boat:3272050086:1371408251:21" => "",
    "Game_Map#ship:1631018288:1387333514:21" => "",
    "Game_Map#update_events:3280958066:797714417:104" => "",
    "Game_Map#setup_autorun_common_event:1759215688:2925478940:183" => "",
    "Game_Vehicle#screen_y:3716450459:2668300970:29" => "",
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
    "Game_Switches#[]:3617574543:1390219188:43" => "",
    # Yanfly's Stop All Movement (YEA::UTILITY switch checks)
    "Switch.self#stop_npc_movement:278151810:2788040413:88" => "yanfly",
    "Game_Event#update_self_movement:2028593028:4135462313:95" => "yanfly",
    # the default without its near_the_screen? check (Ao Oni R)
    "Game_Event#update_self_movement:1974632479:4123803390:168" => "always",
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
    # CanariAlternate Multilayer (natives, and the methods they check are still these)
    "Sprite_Character#update_junction_sprite:2466122098:705765156:4402" => "ml",
    "Sprite_Character#reset_sprite_origin:3137694480:825365155:55" => "ml",
    "Sprite_Character#dispose_junction_sprites:2128924900:1886407892:205" => "ml",
    "Sprite_Character#multi_viewport:3405174318:3420395489:60" => "ml",
    "Sprite_Character#valid_x?:3170161718:3100512405:52" => "known",
    "Sprite_Character#valid_y?:541466540:3386577138:53" => "known",
    "Game_Map#floor_xy:969204233:977108497:383" => "known",
    "Game_Map#round_x:3717177633:1181881102:57" => "",
    "Game_Map#round_y:2321096418:1017254612:57" => "",
    # Event Jitter Fix (Game_Map display_x/y rounded to 1/32, adjust_x/y read them)
    "Game_Map#display_x:1717768432:2265321294:47" => "jitter",
    "Game_Map#display_y:848227820:2269122384:47" => "jitter",
    "Game_Map#adjust_x:1553845420:2760518742:132" => "jitter",
    "Game_Map#adjust_y:2520378825:2320706593:132" => "jitter",
    # Yanfly Ace Core Engine
    "Game_Event#near_the_screen?:2967297709:769812868:342" => "yanfly",
    # LISA: shift_y (object_character? ? 0 : 0), Galv's Region Effects update_events / update_characters
    "Game_CharacterBase#shift_y:545188363:45878379:37" => "lisa",
    "Game_Map#update_events:2662315626:2040473302:99" => "galv",
    "Spriteset_Map#update_characters:571006071:3038915534:112" => "galv",
    # DMO Parallax Animation screen_z(value = nil)
    "Game_CharacterBase#screen_z:2091145282:150221887:147" => "dmo",
    # Hime's Any Key Pressed Check, Zale's Keyboard Manager (Input.update)
    "Input.self#update:2414362333:3074637570:176" => "hime",
    "Input.self#update:1279336715:3637752558:537" => "zale",
    # Victor Engine Character Control (poses; LISA: The Joyful)
    "Game_CharacterBase#update_animation:1015179229:4226047143:272" => "vic",
    "Game_CharacterBase#update_anime_count:1699064850:2066622519:108" => "vic",
    "Game_CharacterBase#update_anime_pattern:2133774055:537802099:146" => "vic",
    "Game_CharacterBase#update_stop:4196311303:3962116204:61" => "vic",
    "Game_CharacterBase#resting_pose:1654497244:1833380308:114" => "known",
    "Sprite_Character#graphic_changed?:2495157378:835003697:98" => "vic",
    # star aura effect (星潟)
    "Sprite#star_aura_effects_update:2583269496:1939184202:2488" => "aura",
    "StarAuraEffect#update_aura_effect:65509816:2849013802:1804" => "known",
    "StarAuraEffect#aura_effect_data:140744705:4159050543:51" => "known",
    "StarAuraEffect#aura_effect_data_delete_opacity_zero:4197886861:4049481205:119" => "known",
  }.freeze

  # Star aura effect (星潟): the part of Sprite#star_aura_effects_update after the effect keys are
  # updated, the same text. The native runs it unless the sprite has no aura at all.
  AURA_TAIL = <<~'RUBY'.freeze
    def __nev_aura_tail(aura_target)
        if self.visible && self.opacity > 0
          aura_bitmap_type = nil
          aura_bitmap_name = nil
          aura_bitmap_hue = nil
          aura_bitmap_src_rect = nil
          @star_aura_effect_keys.each {|i|
          if @star_aura_effects[i]
            @star_aura_effects[i] += 1
            @star_aura_effects[i] = 0 if @star_aura_effects[i] >= StarAuraEffect::P[i][:end]
          else
            @star_aura_effects[i] = 0
          end
          a = StarAuraEffect::P[i][:pattern][@star_aura_effects[i]]
          if a
            if !aura_bitmap_type
              if self.is_a?(Sprite_Battler)
                aura_bitmap_type = :b
                aura_bitmap_name = aura_target.battler_name
                aura_bitmap_hue  = aura_target.battler_hue
                aura_bitmap_src_rect = self.src_rect
              elsif self.is_a?(Sprite_Character)
                aura_bitmap_type = :c
                aura_bitmap_name = aura_target.character_name
                aura_bitmap_hue  = 0
                aura_bitmap_src_rect = self.src_rect
              elsif self.is_a?(Sprite_Picture)
                aura_bitmap_type = :p
                aura_bitmap_name = aura_target.name
                aura_bitmap_hue  = 0
                aura_bitmap_src_rect = self.src_rect
              end
            end
            if aura_bitmap_type != :c or aura_target.near_the_screen_for_aura?
              if !aura_bitmap_name.empty?
                a.each {|type|
                effect_hash = aura_target.aura_effect_data_create(
                type,self.zoom_x,self.zoom_y,self.ox,self.oy,
                self.opacity,self.mirror,self.angle,
                aura_bitmap_type,aura_bitmap_name,
                aura_bitmap_hue,aura_bitmap_src_rect)
                if effect_hash
                  if aura_bitmap_type == :b && aura_target.actor?
                    $game_party.aura_effect_actor_ids_add(aura_target.id)
                  end
                  aura_target.aura_effect_data_add(effect_hash)
                end}
              end
            end
          end}
        end
      aura_target.update_aura_effect(self)
      aura_target.aura_effect_data.each_with_index {|effect_hash,i|
      @star_aura_effect_sprites[i] ||= Sprite_StarAuraEffect.new(self,effect_hash)
      @star_aura_effect_sprites[i].update_by_effect_hash(effect_hash)
      }
      aura_target.aura_effect_data_delete_opacity_zero
      @star_aura_effect_sprites.delete_if {|s| 
      s.dispose if s.opacity == 0
      s.disposed?}
    end
    private :__nev_aura_tail
  RUBY

  # Wrapper variants: "Class#method/variant" => the alias the wrapper calls. The native wrapper
  # calls the native that alias points to, so the alias must be a native (C) method of that name.
  WRAPS = {
    "Sprite_Character#update_position/sp1" => :vxace_sp1_update_position,
    "Game_Character#update/symbol" => :_symbol_opacity_update,
    "Game_Event#update/symbol" => :_update_with_symbol,
    "Game_Event#update_self_movement/symbol" => :_update_self_movement_with_symbol,
    "Game_CharacterBase#real_move_speed/8dir" => :real_move_speed_8direction,
    "Game_Event#update/mog" => :mog_anti_lag_update,
    "Game_Map#update_events/galv" => :galv_region_effects_gm_update_events,
    "Spriteset_Map#update_characters/galv" => :galv_region_effect_sm_sb_update_characters,
    "Game_CharacterBase#screen_z/dmo" => :dmo_al_screen_z,
    "Sprite_Character#update/mog" => :mog_anti_lag_update,
    "Game_CharacterBase#update_anime_count/vic" => :update_anime_count_ve_character_control,
    "Game_CharacterBase#update_anime_pattern/vic" => :update_anime_pattern_ve_character_control,
    "Game_CharacterBase#update_stop/vic" => :update_stop_ve_character_control,
    "Sprite_Character#graphic_changed?/vic" => :graphic_changed_ve_character_control?,
  }.freeze

  CLASSES = %w[Game_CharacterBase Game_Character Game_Event Game_Player Game_Map Game_Variables Game_Switches Game_Vehicle
               Sprite_Base Sprite_Character Spriteset_Map Sprite StarAuraEffect Input.self Switch.self].freeze
  ATTRS = { 'Game_CharacterBase' => %i[tile_id character_name character_index pattern direction opacity blend_type
                                       bush_depth transparent animation_id balloon_id current_floor pose_sufix
                                       idle_stance],
            'Game_Map' => %i[display_x display_y],
            'Game_Vehicle' => %i[altitude],
            'Game_Event' => %i[can_update],
            'Game_Character' => %i[star_aura_effect_update],
            'Game_Temp' => %i[multi_viewport_accessor],
            'Game_System' => %i[anti_lag] }.freeze
  # A section is checked only when it opens one of these.
  NEEDLES = (CLASSES + ATTRS.keys).map { |c| c.sub(/\.self\z/, '') }.uniq.flat_map { |c| ["class #{c}", "module #{c}"] }.freeze
  @swapped = []
  @kept = []

  # The methods Norm finds in a section, kept on the memory card by the section's text, so a game's
  # next start skips reading its scripts again (BLACK SOULS II: about 2 s of every start on the Vita).
  # Only the methods of CLASSES are kept; what they mean (swapped or kept) is still decided each start.
  module SectionCache
    VERSION = 3 # change it whenever Norm's output or CLASSES changes
    MAX = 4000  # sections; the ones this start used are kept first

    class << self
      def path
        $hardrpg_native_events_cache || 'ux0:/data/hardrpg/cache/native-events.dat'
      end

      def sections
        @sections ||= begin
          d = (Marshal.load(File.binread(path)) if path && File.file?(path)) rescue nil
          d.is_a?(Hash) && d[:version] == VERSION && d[:sections].is_a?(Hash) ? d[:sections] : {}
        end
      end

      def methods(src)
        k = "#{Zlib.crc32(src)}:#{src.bytesize}"
        (@used ||= {})[k] = true
        hit = sections[k]
        return hit if hit.is_a?(Array)
        @dirty = true
        sections[k] = Norm.methods(src).select { |key, _| CLASSES.include?(key.split('#', 2)[0]) }.to_a
      end

      def save
        return unless @dirty && path
        @dirty = false
        s = sections
        if s.size > MAX
          old = s.keys.reject { |k| @used[k] }
          old.first(s.size - MAX).each { |k| s.delete(k) }
        end
        dir = File.dirname(path)
        [File.dirname(dir), dir].each { |d| Dir.mkdir(d) unless File.directory?(d) }
        File.binwrite("#{path}.tmp", Marshal.dump({ version: VERSION, sections: s }))
        File.rename("#{path}.tmp", path)
      rescue StandardError, NotImplementedError
        nil
      end
    end
  end

  class << self
    attr_reader :swapped, :kept

    # The default text a swapped method had ("crc32:adler32:bytes"), by "Class#name".
    def swapped_digest(key)
      (@swapped_digest || {})[key]
    end

    def status
      return 'off (disabled)' if $hardrpg_native_events == false
      "on (#{@swapped.size} native, #{@kept.size} kept)"
    end

    # Seconds spent here while the game's scripts load, and the span from the first section to the last.
    def timing
      return 'none' unless @first_at
      format('%.0f ms in checks, sections %.0f ms', (@spent || 0) * 1000, (@last_at - @first_at) * 1000)
    end

    def section_loaded(fname, src)
      now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      @first_at ||= now
      section_check(fname, src)
    ensure
      @last_at = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      @spent = (@spent || 0) + (@last_at - now)
    end

    def section_check(fname, src)
      KhasLights.observe(fname, src)
      return if $hardrpg_native_events == false
      return unless NEEDLES.any? { |n| src.include?(n) }
      SectionCache.methods(src).each do |key, (text, line)|
        cname, name = key.split('#', 2)
        variant = STOCK["#{key}:#{Zlib.crc32(text)}:#{Zlib.adler32(text)}:#{text.bytesize}"]
        margins = nil
        if variant.nil? && key == 'Game_Event#near_the_screen?' &&
           text =~ /\Adef near_the_screen\?\(dx = (\d+), dy = (\d+)\)\n/
          # The default body with other default margins (a wider screen): native, with those margins.
          margins = [$1.to_i, $2.to_i]
          plain = text.sub(/\A[^\n]*/, 'def near_the_screen?(dx = 12, dy = 8)')
          variant = 'margins' if STOCK["#{key}:#{Zlib.crc32(plain)}:#{Zlib.adler32(plain)}:#{plain.bytesize}"] == ''
        end
        base = cname.sub(/\.self\z/, '')
        klass = Object.const_defined?(base) ? Object.const_get(base) : nil
        next unless klass.is_a?(Module)
        klass = klass.singleton_class if base != cname
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
        if variant == 'known'
          # A script method the natives check is still in place (they never replace it).
          known(klass, name.to_sym) if defined_here
          next
        end
        if variant && defined_here && klass.public_method_defined?(name, false)
          klass.class_eval(AURA_TAIL, __FILE__, __LINE__) if variant == 'aura'
          if replace(klass, name.to_sym, variant, cname)
            set_option('near_the_screen_margins', margins) if variant == 'margins'
            @swapped << key
            (@swapped_digest ||= {})[key] = "#{Zlib.crc32(text)}:#{Zlib.adler32(text)}:#{text.bytesize}"
            (@variant_of ||= {})[key] = variant
          else
            @kept << "#{key} (no native)"
          end
        elsif STOCK.keys.any? { |k| k.start_with?("#{key}:") }
          @kept << key # a method we know, but this game's text differs
        end
      end
      lines = nil
      ATTRS.each do |cname, names|
        next unless src.include?("class #{cname}") && Object.const_defined?(cname)
        klass = Object.const_get(cname)
        names.each do |n|
          um = (klass.instance_method(n) rescue nil)
          loc = um && um.owner.equal?(klass) && um.source_location
          next unless loc && loc[0] == fname.to_s && (lines ||= src.lines)[loc[1] - 1].to_s =~ /\A\s*attr_(reader|accessor)\s.*:#{n}\b/
          attr(klass, n)
        end
      end
    end
  end
end

# The game's Main section calls rgss_main once its other sections have run: keep what was read then.
if Kernel.respond_to?(:rgss_main, true)
  Kernel.send(:alias_method, :hardrpg_native_events_rgss_main, :rgss_main)
  Kernel.send(:private, :hardrpg_native_events_rgss_main)
  Kernel.send(:define_method, :rgss_main) do |*args, &block|
    HardRPGNative::SectionCache.save
    HardRPGNative::KhasLights.install
    hardrpg_native_events_rgss_main(*args, &block)
  end
  Kernel.send(:private, :rgss_main)
end
