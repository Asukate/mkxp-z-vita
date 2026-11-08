/*
** sprite-scripts.cpp
**
** Native versions of popular per-sprite scripts that wrap Sprite_Character's steps. With many
** events on screen these wrappers are most of a frame on the Vita (Ao Oni R: about 110 us per
** sprite). launcher/vxace_map_speedups.rb (HardRPGSpriteScripts) checks each script's exact text
** and that every method below and every alias it calls still resolve to that text, then prepends
** the matching module to Sprite_Character.
**
** Each method does what the script's Ruby does, calling the same methods (through Ruby, so a
** game's own versions run) in the same order. Cases the native code does not cover go to the
** script's own method (super).
**
**   TallSprite  - Tall Sprite Fix (update_bitmap, update_position, sprite_is_onscreen?,
**                 check_encompassed_area)
**   ExtraFrames - Modern Algebra's Extra Movement Frames (update_src_rect)
**   ShakeZoom   - Hudell's Character Effects under Hime's Character Sprite Zoom (update_other)
*/

#include <ruby.h>
#include <cmath>

static ID id_character_iv, id_set_upper_iv, id_force_iv, id_upper_sprite_iv, id_emf_char_iv,
    id_origin_x_iv, id_origin_y_iv;
static ID id_graphic_changed, id_cp_update_bitmap, id_cp_update_pos, id_sprite_is_onscreen,
    id_check_encompassed_area, id_is_a, id_large_sprite, id_x, id_y, id_ox, id_oy, id_width,
    id_height, id_display_x, id_display_y, id_jump_height, id_screen_y, id_terrain_tag,
    id_set_visible, id_ma_update_emf_src_rect, id_maemf_update_src_rect, id_th_update_other,
    id_update_origin, id_hudell_update_other, id_character, id_angle, id_set_angle, id_flash_duration,
    id_update_flash, id_zoom_x, id_zoom_y, id_set_zoom_x, id_set_zoom_y, id_origin_x, id_origin_y,
    id_Game_Vehicle, id_Game_Follower, id_Game_Player, id_UpperTerrain, id_Tuning, id_gt, id_neq;
static VALUE g_spriteCharacter = Qnil;

/* obj.is_a?(Const) inside class Sprite_Character. */
static bool isA(VALUE obj, ID constant) {
    VALUE klass = rb_const_get(g_spriteCharacter, constant);
    if (rb_method_basic_definition_p(CLASS_OF(obj), id_is_a))
        return RTEST(rb_obj_is_kind_of(obj, klass));
    return RTEST(rb_funcall(obj, id_is_a, 1, klass));
}

/* ---- Tall Sprite Fix ---------------------------------------------------------------------- */

/* def update_bitmap(*args)
 *   if graphic_changed? && @set_upper_area_sprite then @force_no_gfx_change = true
 *   else @force_no_gfx_change = false end
 *   cp_011214_update_bitmap(*args)
 * end */
static VALUE tall_update_bitmap(int argc, VALUE *argv, VALUE self) {
    bool force = RTEST(rb_funcall(self, id_graphic_changed, 0)) && RTEST(rb_ivar_get(self, id_set_upper_iv));
    rb_ivar_set(self, id_force_iv, force ? Qtrue : Qfalse);
    return rb_funcallv(self, id_cp_update_bitmap, argc, argv);
}

/* def update_position(*args)
 *   cp_073013_update_pos(*args)
 *   check_encompassed_area if sprite_is_onscreen?
 * end */
static VALUE tall_update_position(int argc, VALUE *argv, VALUE self) {
    rb_funcallv(self, id_cp_update_pos, argc, argv);
    if (RTEST(rb_funcall(self, id_sprite_is_onscreen, 0)))
        return rb_funcall(self, id_check_encompassed_area, 0);
    return Qnil;
}

/* The first three lines of sprite_is_onscreen?; the rest is the script's own. Nothing here
 * changes state, so the script repeats them without a difference. */
static VALUE tall_sprite_is_onscreen(VALUE self) {
    if (NIL_P(g_spriteCharacter))
        return rb_call_super(0, nullptr);
    VALUE c = rb_ivar_get(self, id_character_iv);
    if (isA(c, id_Game_Vehicle) || isA(c, id_Game_Follower))
        return Qfalse;
    if (!isA(c, id_Game_Player) && !RTEST(rb_funcall(c, id_large_sprite, 0)))
        return Qfalse;
    return rb_call_super(0, nullptr);
}

static inline long floorDiv(long a, long b) {
    long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/* A Ruby Integer (Fixnum) or Float as a double; false for anything else. */
static bool numOf(VALUE v, double &out, bool &isInt) {
    if (FIXNUM_P(v)) { out = (double)FIX2LONG(v); isInt = true; return true; }
    if (RB_FLOAT_TYPE_P(v)) { out = RFLOAT_VALUE(v); isInt = false; return std::isfinite(out); }
    return false;
}

/* Float#to_i / Integer#to_i within long range. */
static bool toI(VALUE v, long &out) {
    if (FIXNUM_P(v)) { out = FIX2LONG(v); return true; }
    if (RB_FLOAT_TYPE_P(v)) {
        double d = RFLOAT_VALUE(v);
        if (!std::isfinite(d) || std::fabs(d) > 1e15)
            return false;
        out = (long)std::trunc(d);
        return true;
    }
    return false;
}

/* check_encompassed_area when the sprite has no upper copy and no column reaches an upper tile:
 * then the script only sets @set_upper_area_sprite = false (already false), reads terrain tags,
 * and hides the upper sprite if there is one. Anything else (an upper copy to restore or to make,
 * values that are not plain numbers) runs the script's own method. */
static VALUE tall_check_encompassed_area(VALUE self) {
    if (NIL_P(g_spriteCharacter) || RTEST(rb_ivar_get(self, id_set_upper_iv)))
        return rb_call_super(0, nullptr);
    VALUE c = rb_ivar_get(self, id_character_iv);
    VALUE map = rb_gv_get("$game_map");
    VALUE vUpper = rb_const_get(g_spriteCharacter, id_UpperTerrain);
    VALUE vTuning = rb_const_get(g_spriteCharacter, id_Tuning);
    /* get_edge_corner_dis: [x - ox, y - oy], [that + width, that + height] */
    VALUE vx = rb_funcall(self, id_x, 0), vox = rb_funcall(self, id_ox, 0);
    VALUE vy = rb_funcall(self, id_y, 0), voy = rb_funcall(self, id_oy, 0);
    VALUE vw = rb_funcall(self, id_width, 0), vh = rb_funcall(self, id_height, 0);
    VALUE vdx = rb_funcall(map, id_display_x, 0), vdy = rb_funcall(map, id_display_y, 0);
    VALUE vjh = rb_funcall(c, id_jump_height, 0);
    VALUE vsy = rb_funcall(c, id_screen_y, 0);
    if (!FIXNUM_P(vx) || !FIXNUM_P(vox) || !FIXNUM_P(vy) || !FIXNUM_P(voy) || !FIXNUM_P(vw) ||
        !FIXNUM_P(vh) || !FIXNUM_P(vjh) || !FIXNUM_P(vTuning) || !FIXNUM_P(vUpper))
        return rb_call_super(0, nullptr);
    double dx, dy, sy;
    bool dxInt, dyInt, syInt;
    if (!numOf(vdx, dx, dxInt) || !numOf(vdy, dy, dyInt) || !numOf(vsy, sy, syInt))
        return rb_call_super(0, nullptr);
    const long tlx = FIX2LONG(vx) - FIX2LONG(vox), tly = FIX2LONG(vy) - FIX2LONG(voy);
    const long w = FIX2LONG(vw), h = FIX2LONG(vh);
    const long bry = tly + h;
    const long jh = FIX2LONG(vjh), tuning = FIX2LONG(vTuning), upper = FIX2LONG(vUpper);
    if (w > 4096 || h > 4096 || std::fabs(dx) > 1e9 || std::fabs(dy) > 1e9 || std::fabs(sy) > 1e9)
        return rb_call_super(0, nullptr);
    /* map_xd, map_yd = display_x * 32, display_y * 32; .to_i where the script uses it */
    const double mxd = dx * 32, myd = dy * 32;
    const long mx = (long)std::trunc(mxd), my = (long)std::trunc(myd);
    /* (height + jump_height).round */
    const long totalHeight = h + jh;
    /* (screen_y + jump_height + Tuning + map_yd).to_i / 32, left to right as Ruby adds */
    long charRow;
    if (syInt && dyInt) {
        charRow = floorDiv((long)sy + jh + tuning + (long)myd, 32);
    } else {
        double s;
        if (syInt) s = (double)((long)sy + jh + tuning) + myd;
        else s = ((sy + (double)jh) + (double)tuning) + myd;
        charRow = floorDiv((long)std::trunc(s), 32);
    }
    bool haveLastX = false;
    long lastX = 0;
    for (long x = 0; x < w; ++x) {
        const long xp = mx + tlx + x;
        if (haveLastX && floorDiv(xp, 32) == lastX)
            continue;
        haveLastX = true;
        lastX = floorDiv(xp, 32);
        bool haveLastY = false;
        long lastY = 0;
        for (long y = 0; y < totalHeight; ++y) {
            const long yp = my + bry + jh - y + tuning;
            const long row = floorDiv(yp, 32);
            if (haveLastY && row == lastY)
                continue;
            haveLastY = true;
            lastY = row;
            VALUE tag = rb_funcall(map, id_terrain_tag, 2, LONG2NUM(lastX), LONG2NUM(lastY));
            const bool isUpper = FIXNUM_P(tag) ? FIX2LONG(tag) == upper : RTEST(rb_equal(tag, vUpper));
            if (lastY == charRow) {
                if (isUpper)
                    break;
                continue;
            }
            if (!isUpper)
                continue;
            /* An upper tile over the sprite: the script copies part of it. */
            return rb_call_super(0, nullptr);
        }
    }
    rb_ivar_set(self, id_set_upper_iv, Qfalse);
    VALUE up = rb_ivar_get(self, id_upper_sprite_iv);
    if (RTEST(up)) {
        rb_funcall(up, id_set_visible, 1, Qfalse);
        return Qfalse; /* the value of `@upper_area_sprite.visible = false` */
    }
    return Qnil;
}

/* ---- Extra Movement Frames ---------------------------------------------------------------- */

/* def update_src_rect(*args)
 *   @emf_char ? ma_update_emf_src_rect : maemf_updtsrcrect_2kq5(*args)
 * end */
static VALUE emf_update_src_rect(int argc, VALUE *argv, VALUE self) {
    if (RTEST(rb_ivar_get(self, id_emf_char_iv)))
        return rb_funcall(self, id_ma_update_emf_src_rect, 0);
    return rb_funcallv(self, id_maemf_update_src_rect, argc, argv);
}

/* ---- Character Effects under Sprite Zoom -------------------------------------------------- */

/* Hime's zoom wrapper over Hudell's update_other:
 *   def update_other                              # Hime
 *     th_character_sprite_zoom_update_other       #   Hudell's:
 *       update_origin                             #     (unchanged origin: only stores it)
 *       hudell_update_other                       #     the default update_other
 *       self.angle = character.angle
 *       update_flash                              #     (no flash: nothing)
 *     self.zoom_x = @character.zoom_x
 *     self.zoom_y = @character.zoom_y
 *   end */
static bool sameNum(VALUE a, VALUE b) {
    return FIXNUM_P(a) && FIXNUM_P(b) && a == b;
}
static VALUE shake_zoom_update_other(VALUE self) {
    /* update_origin */
    if (NIL_P(rb_ivar_get(self, id_origin_x_iv))) rb_ivar_set(self, id_origin_x_iv, INT2FIX(0));
    if (NIL_P(rb_ivar_get(self, id_origin_y_iv))) rb_ivar_set(self, id_origin_y_iv, INT2FIX(0));
    VALUE ch = rb_funcall(self, id_character, 0);
    VALUE cox = rb_funcall(ch, id_origin_x, 0), coy = rb_funcall(ch, id_origin_y, 0);
    if (sameNum(rb_ivar_get(self, id_origin_x_iv), cox) && sameNum(rb_ivar_get(self, id_origin_y_iv), coy)) {
        rb_ivar_set(self, id_origin_x_iv, cox);
        rb_ivar_set(self, id_origin_y_iv, coy);
    } else {
        rb_funcall(self, id_update_origin, 0);
    }
    rb_funcall(self, id_hudell_update_other, 0);
    rb_funcall(self, id_set_angle, 1, rb_funcall(rb_funcall(self, id_character, 0), id_angle, 0));
    /* update_flash: if character.flash_duration > 0 ... */
    VALUE fd = rb_funcall(rb_funcall(self, id_character, 0), id_flash_duration, 0);
    if (FIXNUM_P(fd) ? FIX2LONG(fd) > 0 : RTEST(rb_funcall(fd, id_gt, 1, INT2FIX(0))))
        rb_funcall(self, id_update_flash, 0);
    VALUE c = rb_ivar_get(self, id_character_iv);
    rb_funcall(self, id_set_zoom_x, 1, rb_funcall(c, id_zoom_x, 0));
    c = rb_ivar_get(self, id_character_iv);
    VALUE zy = rb_funcall(c, id_zoom_y, 0);
    rb_funcall(self, id_set_zoom_y, 1, zy);
    return zy;
}

/* ---- Galv's Character Effects: Sprite_Shadow --------------------------------------------- */

static ID id_screen_x, id_real_x, id_real_y, id_light_source, id_shadow_options, id_set_x, id_set_y,
    id_opacity, id_set_opacity, id_set_mirror, id_blend_type, id_set_blend_type, id_transparent, id_aref,
    id_minus, id_plus, id_mul, id_div, id_lt, id_le, id_eq, id_source_iv, id_aamount_iv;

/* a - b, a * b, ... as Ruby does them; Integer and Float fast, anything else through Ruby. */
static bool plainNum(VALUE v) { return FIXNUM_P(v) || RB_FLOAT_TYPE_P(v); }
static VALUE numOp(VALUE a, ID op, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b)) {
        long x = FIX2LONG(a), y = FIX2LONG(b);
        if (op == id_minus) return LONG2NUM(x - y);
        if (op == id_plus) return LONG2NUM(x + y);
        if (op == id_mul && x > -32768 && x < 32768 && y > -32768 && y < 32768) return LONG2NUM(x * y);
    } else if (plainNum(a) && plainNum(b) && op != id_div) {
        double x = FIXNUM_P(a) ? (double)FIX2LONG(a) : RFLOAT_VALUE(a);
        double y = FIXNUM_P(b) ? (double)FIX2LONG(b) : RFLOAT_VALUE(b);
        if (op == id_minus) return DBL2NUM(x - y);
        if (op == id_plus) return DBL2NUM(x + y);
        if (op == id_mul) return DBL2NUM(x * y);
    }
    return rb_funcall(a, op, 1, b);
}
static double toDbl(VALUE v) { return FIXNUM_P(v) ? (double)FIX2LONG(v) : RFLOAT_VALUE(v); }

/* def get_angle
 *   x = $game_map.light_source[@source][0] - @character.real_x
 *   y = $game_map.light_source[@source][1] - @character.real_y
 *   self.opacity = $game_map.shadow_options[0] - Math::sqrt(x * x + y * y) * $game_map.shadow_options[1]
 *   if x == 0 && y == 0 || self.opacity <= 0
 *     self.opacity = 0
 *   else
 *     self.angle = Math::atan2(x, y) * 180 / Math::PI + @aamount
 *   end
 * end */
static VALUE shadow_get_angle(VALUE self) {
    VALUE gm = rb_gv_get("$game_map");
    VALUE src = rb_ivar_get(self, id_source_iv);
    VALUE c = rb_ivar_get(self, id_character_iv);
    VALUE lx = rb_funcall(rb_funcall(rb_funcall(gm, id_light_source, 0), id_aref, 1, src), id_aref, 1, INT2FIX(0));
    VALUE x = numOp(lx, id_minus, rb_funcall(c, id_real_x, 0));
    VALUE ly = rb_funcall(rb_funcall(rb_funcall(gm, id_light_source, 0), id_aref, 1, src), id_aref, 1, INT2FIX(1));
    c = rb_ivar_get(self, id_character_iv);
    VALUE y = numOp(ly, id_minus, rb_funcall(c, id_real_y, 0));
    VALUE s0 = rb_funcall(rb_funcall(gm, id_shadow_options, 0), id_aref, 1, INT2FIX(0));
    VALUE sum = numOp(numOp(x, id_mul, x), id_plus, numOp(y, id_mul, y));
    VALUE root = plainNum(sum) ? DBL2NUM(std::sqrt(toDbl(sum))) : rb_funcall(rb_mMath, rb_intern("sqrt"), 1, sum);
    VALUE s1 = rb_funcall(rb_funcall(gm, id_shadow_options, 0), id_aref, 1, INT2FIX(1));
    rb_funcall(self, id_set_opacity, 1, numOp(s0, id_minus, numOp(root, id_mul, s1)));
    bool zero = RTEST(rb_funcall(x, id_eq, 1, INT2FIX(0))) && RTEST(rb_funcall(y, id_eq, 1, INT2FIX(0)));
    if (zero || RTEST(rb_funcall(rb_funcall(self, id_opacity, 0), id_le, 1, INT2FIX(0))))
        return rb_funcall(self, id_set_opacity, 1, INT2FIX(0));
    VALUE at = (plainNum(x) && plainNum(y)) ? DBL2NUM(std::atan2(toDbl(x), toDbl(y)))
                                            : rb_funcall(rb_mMath, rb_intern("atan2"), 2, x, y);
    VALUE pi = rb_const_get(rb_mMath, rb_intern("PI"));
    VALUE deg = rb_funcall(numOp(at, id_mul, INT2FIX(180)), id_div, 1, pi);
    VALUE v = numOp(deg, id_plus, rb_ivar_get(self, id_aamount_iv));
    rb_funcall(self, id_set_angle, 1, v);
    return v;
}
/* def update_position
 *   self.x = @character.screen_x
 *   self.y = @character.screen_y - 10
 *   get_angle
 * end  (get_angle through Ruby: a game's own version runs) */
static VALUE shadow_update_position(VALUE self) {
    rb_funcall(self, id_set_x, 1, rb_funcall(rb_ivar_get(self, id_character_iv), id_screen_x, 0));
    rb_funcall(self, id_set_y, 1, numOp(rb_funcall(rb_ivar_get(self, id_character_iv), id_screen_y, 0), id_minus, INT2FIX(10)));
    return rb_funcall(self, rb_intern("get_angle"), 0);
}
/* def update_facing
 *   if @character.y < $game_map.light_source[@source][1] then self.mirror = false
 *   else self.mirror = true end
 * end */
static VALUE shadow_update_facing(VALUE self) {
    VALUE cy = rb_funcall(rb_ivar_get(self, id_character_iv), id_y, 0);
    VALUE ly = rb_funcall(rb_funcall(rb_funcall(rb_gv_get("$game_map"), id_light_source, 0), id_aref, 1,
                                     rb_ivar_get(self, id_source_iv)), id_aref, 1, INT2FIX(1));
    bool lt = (FIXNUM_P(cy) && FIXNUM_P(ly)) ? FIX2LONG(cy) < FIX2LONG(ly) : RTEST(rb_funcall(cy, id_lt, 1, ly));
    return rb_funcall(self, id_set_mirror, 1, lt ? Qfalse : Qtrue);
}
/* def update_other
 *   self.blend_type = @character.blend_type
 *   self.visible = !@character.transparent
 * end */
static VALUE shadow_update_other(VALUE self) {
    rb_funcall(self, id_set_blend_type, 1, rb_funcall(rb_ivar_get(self, id_character_iv), id_blend_type, 0));
    VALUE t = rb_funcall(rb_ivar_get(self, id_character_iv), id_transparent, 0);
    return rb_funcall(self, id_set_visible, 1, RTEST(t) ? Qfalse : Qtrue);
}

/* Galv's Sprite_Shadow#update, parked off screen: binding/native-events.cpp decides (option
 * shadow_park); otherwise the script's own update. */
bool (*hardrpgShadowParkHook)(VALUE) = nullptr;
static VALUE shadow_park_update(VALUE self) {
    if (hardrpgShadowParkHook && hardrpgShadowParkHook(self))
        return Qnil;
    return rb_call_super(0, nullptr);
}

/* ---- per-event wrappers ------------------------------------------------------------------- */

static ID id_th_zooming_update, id_update_zoom, id_is_zooming_iv, id_madsee_alias, id_madsee_iv, id_update;

/* Hime's Character Sprite Zoom, class Game_Character:
 *   alias :th_character_sprite_zooming_update :update
 *   def update
 *     th_character_sprite_zooming_update
 *     update_zoom
 *   end
 * update_zoom starts with `return unless @is_zooming`: only a zooming character calls it. */
static VALUE zoom_character_update(VALUE self) {
    rb_funcall(self, id_th_zooming_update, 0);
    if (!RTEST(rb_ivar_get(self, id_is_zooming_iv)))
        return Qnil;
    return rb_funcall(self, id_update_zoom, 0);
}
/* Modern Algebra's Sound Emitting Events, class Game_Event:
 *   alias madsee_updt_1ex0 update
 *   def update(*args)
 *     madsee_updt_1ex0(*args)
 *     @madsee_emitter.update if @madsee_emitter
 *   end */
static VALUE madsee_event_update(int argc, VALUE *argv, VALUE self) {
    rb_funcallv(self, id_madsee_alias, argc, argv);
    VALUE e = rb_ivar_get(self, id_madsee_iv);
    return RTEST(e) ? rb_funcall(e, id_update, 0) : Qnil;
}

/* HardRPGNative::SpriteScripts.bind(Sprite_Character): the class whose constants the scripts
 * read (UpperTerrain, Tuning, Game_Player...), as their code inside that class does. */
static VALUE sprite_scripts_bind(VALUE, VALUE klass) {
    Check_Type(klass, T_CLASS);
    g_spriteCharacter = klass;
    return klass;
}

void spriteScriptsInit() {
    id_character_iv = rb_intern("@character");
    id_set_upper_iv = rb_intern("@set_upper_area_sprite");
    id_force_iv = rb_intern("@force_no_gfx_change");
    id_upper_sprite_iv = rb_intern("@upper_area_sprite");
    id_emf_char_iv = rb_intern("@emf_char");
    id_origin_x_iv = rb_intern("@origin_x");
    id_origin_y_iv = rb_intern("@origin_y");
    id_graphic_changed = rb_intern("graphic_changed?");
    id_cp_update_bitmap = rb_intern("cp_011214_update_bitmap");
    id_cp_update_pos = rb_intern("cp_073013_update_pos");
    id_sprite_is_onscreen = rb_intern("sprite_is_onscreen?");
    id_check_encompassed_area = rb_intern("check_encompassed_area");
    id_is_a = rb_intern("is_a?");
    id_large_sprite = rb_intern("large_sprite");
    id_x = rb_intern("x");
    id_y = rb_intern("y");
    id_ox = rb_intern("ox");
    id_oy = rb_intern("oy");
    id_width = rb_intern("width");
    id_height = rb_intern("height");
    id_display_x = rb_intern("display_x");
    id_display_y = rb_intern("display_y");
    id_jump_height = rb_intern("jump_height");
    id_screen_y = rb_intern("screen_y");
    id_terrain_tag = rb_intern("terrain_tag");
    id_set_visible = rb_intern("visible=");
    id_ma_update_emf_src_rect = rb_intern("ma_update_emf_src_rect");
    id_maemf_update_src_rect = rb_intern("maemf_updtsrcrect_2kq5");
    id_th_update_other = rb_intern("th_character_sprite_zoom_update_other");
    id_update_origin = rb_intern("update_origin");
    id_hudell_update_other = rb_intern("hudell_update_other");
    id_character = rb_intern("character");
    id_angle = rb_intern("angle");
    id_set_angle = rb_intern("angle=");
    id_flash_duration = rb_intern("flash_duration");
    id_update_flash = rb_intern("update_flash");
    id_zoom_x = rb_intern("zoom_x");
    id_zoom_y = rb_intern("zoom_y");
    id_set_zoom_x = rb_intern("zoom_x=");
    id_set_zoom_y = rb_intern("zoom_y=");
    id_origin_x = rb_intern("origin_x");
    id_origin_y = rb_intern("origin_y");
    id_Game_Vehicle = rb_intern("Game_Vehicle");
    id_Game_Follower = rb_intern("Game_Follower");
    id_Game_Player = rb_intern("Game_Player");
    id_UpperTerrain = rb_intern("UpperTerrain");
    id_Tuning = rb_intern("Tuning");
    id_gt = rb_intern(">");
    id_neq = rb_intern("!=");

    id_screen_x = rb_intern("screen_x");
    id_real_x = rb_intern("real_x");
    id_real_y = rb_intern("real_y");
    id_light_source = rb_intern("light_source");
    id_shadow_options = rb_intern("shadow_options");
    id_set_x = rb_intern("x=");
    id_set_y = rb_intern("y=");
    id_opacity = rb_intern("opacity");
    id_set_opacity = rb_intern("opacity=");
    id_set_mirror = rb_intern("mirror=");
    id_blend_type = rb_intern("blend_type");
    id_set_blend_type = rb_intern("blend_type=");
    id_transparent = rb_intern("transparent");
    id_aref = rb_intern("[]");
    id_minus = rb_intern("-");
    id_plus = rb_intern("+");
    id_mul = rb_intern("*");
    id_div = rb_intern("/");
    id_lt = rb_intern("<");
    id_le = rb_intern("<=");
    id_eq = rb_intern("==");
    id_source_iv = rb_intern("@source");
    id_aamount_iv = rb_intern("@aamount");

    id_th_zooming_update = rb_intern("th_character_sprite_zooming_update");
    id_update_zoom = rb_intern("update_zoom");
    id_is_zooming_iv = rb_intern("@is_zooming");
    id_madsee_alias = rb_intern("madsee_updt_1ex0");
    id_madsee_iv = rb_intern("@madsee_emitter");
    id_update = rb_intern("update");

    VALUE native = rb_define_module("HardRPGNative");
    VALUE top = rb_define_module_under(native, "SpriteScripts");
    VALUE tall = rb_define_module_under(top, "TallSprite");
    rb_define_method(tall, "update_bitmap", RUBY_METHOD_FUNC(tall_update_bitmap), -1);
    rb_define_method(tall, "update_position", RUBY_METHOD_FUNC(tall_update_position), -1);
    rb_define_method(tall, "sprite_is_onscreen?", RUBY_METHOD_FUNC(tall_sprite_is_onscreen), 0);
    rb_define_method(tall, "check_encompassed_area", RUBY_METHOD_FUNC(tall_check_encompassed_area), 0);
    VALUE emf = rb_define_module_under(top, "ExtraFrames");
    rb_define_method(emf, "update_src_rect", RUBY_METHOD_FUNC(emf_update_src_rect), -1);
    rb_gc_register_address(&g_spriteCharacter);
    rb_define_singleton_method(top, "bind", RUBY_METHOD_FUNC(sprite_scripts_bind), 1);
    VALUE sz = rb_define_module_under(top, "ShakeZoom");
    rb_define_method(sz, "update_other", RUBY_METHOD_FUNC(shake_zoom_update_other), 0);
    VALUE gp = rb_define_module_under(top, "GalvShadowPark");
    rb_define_method(gp, "update", RUBY_METHOD_FUNC(shadow_park_update), 0);
    VALUE zu = rb_define_module_under(top, "ZoomUpdate");
    rb_define_method(zu, "update", RUBY_METHOD_FUNC(zoom_character_update), 0);
    VALUE mu = rb_define_module_under(top, "MadseeUpdate");
    rb_define_method(mu, "update", RUBY_METHOD_FUNC(madsee_event_update), -1);
    VALUE galv = rb_define_module_under(top, "GalvShadow");
    rb_define_method(galv, "update_position", RUBY_METHOD_FUNC(shadow_update_position), 0);
    rb_define_method(galv, "get_angle", RUBY_METHOD_FUNC(shadow_get_angle), 0);
    rb_define_method(galv, "update_facing", RUBY_METHOD_FUNC(shadow_update_facing), 0);
    rb_define_method(galv, "update_other", RUBY_METHOD_FUNC(shadow_update_other), 0);
}

