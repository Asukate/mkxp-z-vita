/*
 ** native-events.cpp
 **
 ** Native versions of the default RPG Maker VX Ace map character code
 ** (Game_CharacterBase, Game_Character, Game_Event, Game_Map, Sprite_Base,
 ** Sprite_Character and Spriteset_Map). With hundreds of events on a map,
 ** running these small methods in Ruby is most of a frame on the Vita.
 **
 ** How a game keeps its own behaviour:
 ** - A method is replaced only if the game's script section still holds the
 **   default text of that method (checked by launcher/native_events.rb right
 **   after the section ran, before any later script can alias it). Scripts
 **   that alias a default method and add to it then wrap the native one.
 ** - Inside native code, each call to another method (on the character, the
 **   map, the sprite, or an engine object) first checks that the object's
 **   class still resolves that name to the default or native method. If a
 **   game replaced it, the call goes through Ruby as before.
 ** - Arithmetic keeps Ruby's types: Integer stays Integer, Float stays
 **   Float, with Ruby's rounding and comparison rules. Anything that is not
 **   a plain Integer or Float goes through Ruby's own operators.
 **
 ** Set $hardrpg_native_events = false in a per-game shim to keep Ruby.
 */

#include "binding-util.h"

#include <cmath>
#include <chrono>
#include "sprite.h"
#include "etc.h"
#include "sharedstate.h"
#include "graphics.h"
#include <SDL.h>
#include <cstring>
#include <map>
#include <unordered_map>
#include <string>
#include <utility>
#include <array>

extern "C" VALUE rb_gvar_get(ID);

/* ------------------------------------------------------------------ */
/* Engine method registry (filled by _rb_define_method & co.)          */

namespace {
struct RecordKey {
    VALUE klass;
    ID mid;
    bool operator<(const RecordKey &o) const {
        return klass < o.klass || (klass == o.klass && mid < o.mid);
    }
};
std::map<RecordKey, RubyMethod> &registry() {
    static std::map<RecordKey, RubyMethod> r;
    return r;
}
} // namespace

void bindingRecordMethod(VALUE klass, const char *name, RubyMethod func) {
    registry()[RecordKey{klass, rb_intern(name)}] = func;
}

static RubyMethod engineMethod(VALUE klass, ID mid) {
    auto it = registry().find(RecordKey{klass, mid});
    return it == registry().end() ? nullptr : it->second;
}

/* ------------------------------------------------------------------ */
/* Method resolution checks                                            */

static uint32_t g_epoch = 1;   /* bumped on any method definition/include */
static VALUE g_module = Qnil;  /* HardRPGNative */
static ID id_resolves, id_super_ok, id_instance_method;

/* Instance variable read with a per-call-site cache, as Ruby's own getinstancevariable does:
 * for a plain object (T_OBJECT) whose ivars live in a separate array, a name's position comes
 * from the class's index table and never changes for that table, so later reads are an array
 * load. Anything else (embedded objects, sprites, other types) uses rb_ivar_get. */
struct IvSite {
    VALUE klass[2];
    const void *tbl[2];
    uint32_t index[2];
    uint8_t next;
};
static inline VALUE ivFast(VALUE o, ID id, IvSite &s) {
    if (!RB_SPECIAL_CONST_P(o) && RB_BUILTIN_TYPE(o) == RUBY_T_OBJECT && !RB_FL_ANY_RAW(o, ROBJECT_EMBED)) {
        VALUE k = RBASIC(o)->klass;
        const void *t = ROBJECT(o)->as.heap.iv_index_tbl;
        for (int j = 0; j < 2; ++j) {
            if (s.klass[j] == k && s.tbl[j] == t) {
                uint32_t i = s.index[j];
                if (i < ROBJECT(o)->as.heap.numiv) {
                    VALUE v = ROBJECT(o)->as.heap.ivptr[i];
                    return v == Qundef ? Qnil : v;
                }
                return Qnil;
            }
        }
        st_data_t ent;
        if (t && st_lookup((st_table *)t, (st_data_t)id, &ent)) {
            int j = s.next++ & 1;
            s.klass[j] = k;
            s.tbl[j] = t;
            s.index[j] = *(const uint32_t *)ent; /* rb_iv_index_tbl_entry.index */
        }
    }
    return rb_ivar_get(o, id);
}
enum SlotKind { SK_NONE, SK_NATIVE, SK_ENGINE, SK_ATTR };

typedef VALUE (*NativeFn)(VALUE self, int argc, const VALUE *argv, bool fromRuby);

struct Slot {
    const char *owner_name;
    const char *name;
    ID mid;
    int kind;
    VALUE owner;
    VALUE expect;       /* UnboundMethod the name must resolve to */
    RubyMethod engine;  /* SK_ENGINE */
    ID ivar;            /* SK_ATTR */
    NativeFn native;    /* SK_NATIVE */
    int superSlot;      /* SK_NATIVE: slot its `super` must reach, or -1 */
    int superAlt;       /* another slot `super` may reach instead (a script's wrapper), or -1 */
    int superFrom;      /* helper slot: the slot whose `super` its native method makes, or -1 */
    /* one-entry cache */
    VALUE cKlass;
    uint32_t cEpoch;
    bool cOk;
    /* super check cache (owner is fixed) */
    uint32_t sEpoch;
    bool sOk;
    int sWhich; /* the slot `super` reaches when sOk */
    /* statistics */
    uint32_t direct, viaRuby;
};

enum SlotId {
    /* Game_CharacterBase */
    S_GCB_moving, S_GCB_jumping, S_GCB_stopping, S_GCB_real_move_speed, S_GCB_distance_per_frame,
    S_GCB_dash, S_GCB_object_character, S_GCB_shift_y, S_GCB_jump_height, S_GCB_screen_x,
    S_GCB_screen_y, S_GCB_screen_z, S_GCB_normal_priority, S_GCB_update, S_GCB_update_move,
    S_GCB_update_stop, S_GCB_update_animation, S_GCB_update_anime_count, S_GCB_update_anime_pattern,
    S_GCB_update_jump, S_GCB_update_bush_depth,
    /* attributes of Game_CharacterBase */
    S_GCB_tile_id, S_GCB_character_name, S_GCB_character_index, S_GCB_pattern, S_GCB_direction,
    S_GCB_opacity, S_GCB_blend_type, S_GCB_bush_depth, S_GCB_transparent, S_GCB_animation_id,
    S_GCB_balloon_id,
    /* Game_Character */
    S_GC_update_stop, S_GC_update_routine_move,
    /* Game_Event */
    S_GE_update, S_GE_update_stop, S_GE_update_self_movement, S_GE_near_the_screen,
    S_GE_stop_count_threshold, S_GE_check_event_trigger_auto, S_GE_move_type_random,
    S_GE_move_type_toward_player, S_GE_move_type_custom, S_GE_start,
    /* popular scripts: symbol encounter, CACAO off-screen movement, 8-direction movement,
     * kure region pass, MOG Anti Lag (and the alias names their wrappers call) */
    S_GC_update, S_GC_update_symbol_opacity, S_GE_update_symbol_opacity, S_GE_self_movable,
    S_GE_update_anti_lag, S_GE_anti_lag_on_screen, S_GE_can_update, S_GV_aref, S_GS_anti_lag,
    S_GM_display_x, S_GM_display_y, S_SC_check_can_update_sprite, S_SPR_visible,
    S_H_GE_update_symbol, S_H_GE_update_mog, S_H_GC_update_symbol, S_H_GE_usm_symbol,
    S_H_GCB_rms_8dir, S_H_SC_update_mog,
    /* Game_Map */
    S_GM_adjust_x, S_GM_adjust_y, S_GM_loop_horizontal, S_GM_loop_vertical, S_GM_width, S_GM_height,
    S_GM_screen_tile_x, S_GM_screen_tile_y, S_GM_update_events,
    /* RPG data attributes (engine Ruby module) */
    S_MAP_scroll_type, S_MAP_width, S_MAP_height, S_EVENT_id,
    /* Sprite_Base */
    S_SB_update, S_SB_animation, S_SB_update_animation,
    /* Sprite_Character */
    S_SC_update, S_SC_update_bitmap, S_SC_graphic_changed, S_SC_update_src_rect, S_SC_update_position,
    S_SC_update_other, S_SC_update_balloon, S_SC_setup_new_effect, S_SC_move_animation,
    S_SC_set_tile_bitmap, S_SC_set_character_bitmap, S_SC_start_animation, S_SC_start_balloon,
    S_SC_sp1_orig,
    /* Spriteset_Map */
    S_SM_update_characters, S_SM_refresh_characters,
    /* engine */
    S_SPR_update, S_SPR_x, S_SPR_y, S_SPR_set_x, S_SPR_set_y, S_SPR_set_z, S_SPR_set_opacity,
    S_SPR_set_blend_type, S_SPR_set_bush_depth, S_SPR_set_visible, S_SPR_src_rect, S_RECT_set,
    S_GFX_width, S_GFX_height, S_ARY_clear, S_STR_eq, S_STR_neq, S_ARY_aref, S_INT_zero,
    S_INT_truncate, S_FLO_truncate, S_INT_between, S_OBJ_is_a,
    S_COUNT
};

static Slot g_slots[S_COUNT];
static IvSite g_attrSites[S_COUNT];
static ID id_src_rect_iv;

static void slotDef(int id, const char *owner, const char *name) {
    Slot &s = g_slots[id];
    s = Slot();
    s.owner_name = owner;
    s.name = name;
    s.mid = rb_intern(name);
    s.kind = SK_NONE;
    s.owner = Qnil;
    s.expect = Qnil;
    s.superSlot = -1;
    s.superAlt = -1;
    s.superFrom = -1;
}

static bool superOk(Slot &s);

static bool computeOk(Slot &s, VALUE klass) {
    if (s.kind == SK_NONE || NIL_P(s.expect))
        return false;
    VALUE args[4] = {klass, ID2SYM(s.mid), s.owner, s.expect};
    return RTEST(rb_funcallv(g_module, id_resolves, 4, args));
}

/* Answers per (class, slot), valid until the next method change anywhere. A frame
 * walks events, vehicles, followers and the player, so each slot sees several classes. */
struct Memo {
    uint32_t epoch;
    bool ok;
};
static std::unordered_map<uint64_t, Memo> g_memo;
static uint32_t g_checks = 0; /* slow (Ruby-level) checks done */

static bool slotOkSlow(Slot &s, int id, VALUE klass) {
    uint64_t key = ((uint64_t)klass << 8) | (uint64_t)id;
    auto it = g_memo.find(key);
    if (it != g_memo.end() && it->second.epoch == g_epoch)
        return it->second.ok;
    g_checks++;
    bool ok = computeOk(s, klass);
    /* A native that calls `super` (or a helper running such a native) is only used directly
     * while that `super` reaches a default/native method too. */
    int sf = s.superFrom >= 0 ? s.superFrom : id;
    if (ok && s.kind == SK_NATIVE && g_slots[sf].superSlot >= 0)
        ok = superOk(g_slots[sf]);
    g_memo[key] = Memo{g_epoch, ok};
    return ok;
}

/* Does `klass` resolve the slot's name to the default/native method? */
static inline bool slotOk(Slot &s, VALUE klass) {
    if (s.cKlass == klass && s.cEpoch == g_epoch)
        return s.cOk;
    bool ok = slotOkSlow(s, (int)(&s - g_slots), klass);
    s.cKlass = klass;
    s.cEpoch = g_epoch;
    s.cOk = ok;
    return ok;
}

/* Does `super` inside this slot's native method reach the default method? */
static bool superOk(Slot &s) {
    if (s.sEpoch == g_epoch)
        return s.sOk;
    bool ok = false;
    for (int ti : {s.superSlot, s.superAlt}) {
        if (ti < 0)
            continue;
        Slot &t = g_slots[ti];
        if (t.kind == SK_NONE || NIL_P(t.expect))
            continue;
        VALUE args[4] = {s.owner, ID2SYM(s.mid), t.owner, t.expect};
        if (RTEST(rb_funcallv(g_module, id_super_ok, 4, args))) {
            ok = true;
            s.sWhich = ti;
            break;
        }
    }
    s.sEpoch = g_epoch;
    s.sOk = ok;
    return ok;
}

/* Call `name` on obj: directly when it is still the default, else through Ruby. */
static VALUE callSlot(VALUE obj, int id, int argc, const VALUE *argv) {
    Slot &s = g_slots[id];
    if (slotOk(s, rb_class_of(obj))) {
        s.direct++;
        switch (s.kind) {
        case SK_ATTR:
            return ivFast(obj, s.ivar, g_attrSites[id]);
        case SK_ENGINE:
            return s.engine(argc, const_cast<VALUE *>(argv), obj);
        case SK_NATIVE:
            return s.native(obj, argc, argv, false);
        }
    }
    s.viaRuby++;
    return rb_funcallv(obj, s.mid, argc, argv);
}

static inline VALUE call0(VALUE obj, int id) { return callSlot(obj, id, 0, nullptr); }
static inline VALUE call1(VALUE obj, int id, VALUE a) { return callSlot(obj, id, 1, &a); }
static inline VALUE call2(VALUE obj, int id, VALUE a, VALUE b) {
    VALUE v[2] = {a, b};
    return callSlot(obj, id, 2, v);
}

/* `super` from inside the native method of slot `id`. */
static VALUE callSuper(VALUE self, int id, int argc, const VALUE *argv, bool fromRuby) {
    Slot &s = g_slots[id];
    if (superOk(s)) {
        Slot &t = g_slots[s.sWhich];
        t.direct++;
        switch (t.kind) {
        case SK_ENGINE:
            return t.engine(argc, const_cast<VALUE *>(argv), self);
        case SK_NATIVE:
            return t.native(self, argc, argv, false);
        case SK_ATTR:
            return rb_ivar_get(self, t.ivar);
        }
    }
    /* Only reached when called from Ruby (a direct call needs superOk). */
    (void)fromRuby;
    return rb_call_super(argc, argv);
}

/* The default Ruby body, kept under a private alias. */
static ID origId(int id) {
    std::string n = std::string("__nev_orig_") + g_slots[id].name;
    return rb_intern(n.c_str());
}
static ID g_orig[S_COUNT];
static inline VALUE callOrig(VALUE self, int id, int argc, const VALUE *argv) {
    return rb_funcallv(self, g_orig[id], argc, argv);
}

/* ------------------------------------------------------------------ */
/* Ruby arithmetic on Integer/Float with Ruby's results                */

static ID id_plus, id_minus, id_mul, id_div, id_mod, id_lt, id_gt, id_le, id_ge, id_eq, id_neq,
    id_uminus, id_pow, id_aref, id_max, id_min, id_running, id_setup, id_update, id_clear, id_abs;

static inline bool isFlo(VALUE v) { return RB_FLOAT_TYPE_P(v); }
static inline double fv(VALUE v) { return FIXNUM_P(v) ? (double)FIX2LONG(v) : RFLOAT_VALUE(v); }
static inline bool plain(VALUE v) { return FIXNUM_P(v) || isFlo(v); }

static VALUE nAdd(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return LONG2NUM(FIX2LONG(a) + FIX2LONG(b));
    if (plain(a) && plain(b) && (isFlo(a) || isFlo(b)))
        return DBL2NUM(fv(a) + fv(b));
    return rb_funcall(a, id_plus, 1, b);
}
static VALUE nSub(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return LONG2NUM(FIX2LONG(a) - FIX2LONG(b));
    if (plain(a) && plain(b) && (isFlo(a) || isFlo(b)))
        return DBL2NUM(fv(a) - fv(b));
    return rb_funcall(a, id_minus, 1, b);
}
static VALUE nMul(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return LL2NUM((long long)FIX2LONG(a) * (long long)FIX2LONG(b));
    if (plain(a) && plain(b) && (isFlo(a) || isFlo(b)))
        return DBL2NUM(fv(a) * fv(b));
    return rb_funcall(a, id_mul, 1, b);
}
static VALUE nDiv(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b) && FIX2LONG(b) != 0) {
        long x = FIX2LONG(a), y = FIX2LONG(b);
        long q = x / y;
        if ((x % y != 0) && ((x < 0) != (y < 0)))
            q -= 1;
        return LONG2NUM(q);
    }
    if (plain(a) && plain(b) && (isFlo(a) || isFlo(b)))
        return DBL2NUM(fv(a) / fv(b));
    return rb_funcall(a, id_div, 1, b);
}
static VALUE nMod(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b) && FIX2LONG(b) != 0) {
        long x = FIX2LONG(a), y = FIX2LONG(b);
        long r = x % y;
        if (r != 0 && ((r < 0) != (y < 0)))
            r += y;
        return LONG2NUM(r);
    }
    return rb_funcall(a, id_mod, 1, b);
}
static VALUE nNeg(VALUE a) {
    if (FIXNUM_P(a))
        return LONG2NUM(-FIX2LONG(a));
    if (isFlo(a))
        return DBL2NUM(-RFLOAT_VALUE(a));
    return rb_funcall(a, id_uminus, 0);
}
/* Comparisons return Ruby's truth value. */
static bool nLt(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return FIX2LONG(a) < FIX2LONG(b);
    if (plain(a) && plain(b))
        return fv(a) < fv(b);
    return RTEST(rb_funcall(a, id_lt, 1, b));
}
static bool nGt(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return FIX2LONG(a) > FIX2LONG(b);
    if (plain(a) && plain(b))
        return fv(a) > fv(b);
    return RTEST(rb_funcall(a, id_gt, 1, b));
}
static bool nLe(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return FIX2LONG(a) <= FIX2LONG(b);
    if (plain(a) && plain(b))
        return fv(a) <= fv(b);
    return RTEST(rb_funcall(a, id_le, 1, b));
}
static bool nGe(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return FIX2LONG(a) >= FIX2LONG(b);
    if (plain(a) && plain(b))
        return fv(a) >= fv(b);
    return RTEST(rb_funcall(a, id_ge, 1, b));
}
static bool nEq(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return a == b;
    if (plain(a) && plain(b))
        return fv(a) == fv(b);
    if (RB_TYPE_P(a, T_STRING) && RB_TYPE_P(b, T_STRING) && rb_class_of(a) == rb_cString &&
        slotOk(g_slots[S_STR_eq], rb_cString))
        return RTEST(rb_str_equal(a, b));
    if (NIL_P(a))
        return NIL_P(b);
    return RTEST(rb_funcall(a, id_eq, 1, b));
}
static bool nNe(VALUE a, VALUE b) {
    if ((FIXNUM_P(a) || isFlo(a) || NIL_P(a)) && (plain(b) || NIL_P(b)))
        return !nEq(a, b);
    if (RB_TYPE_P(a, T_STRING) && RB_TYPE_P(b, T_STRING) && rb_class_of(a) == rb_cString &&
        slotOk(g_slots[S_STR_neq], rb_cString) && slotOk(g_slots[S_STR_eq], rb_cString))
        return !RTEST(rb_str_equal(a, b));
    return RTEST(rb_funcall(a, id_neq, 1, b));
}
static inline bool nanv(VALUE v) { return isFlo(v) && std::isnan(RFLOAT_VALUE(v)); }
/* [a, b].max / [a, b].min: ties keep a. */
static VALUE nMax2(VALUE a, VALUE b) {
    if (plain(a) && plain(b) && !nanv(a) && !nanv(b))
        return nGt(b, a) ? b : a;
    return rb_funcall(rb_ary_new_from_args(2, a, b), id_max, 0);
}
static VALUE nMin2(VALUE a, VALUE b) {
    if (plain(a) && plain(b) && !nanv(a) && !nanv(b))
        return nLt(b, a) ? b : a;
    return rb_funcall(rb_ary_new_from_args(2, a, b), id_min, 0);
}
static inline VALUE rbool(bool b) { return b ? Qtrue : Qfalse; }

/* ------------------------------------------------------------------ */
/* Instance variable ids                                               */

static ID iv_real_x, iv_real_y, iv_x, iv_y, iv_jump_count, iv_jump_peak, iv_move_speed, iv_tile_id,
    iv_character_name, iv_priority_type, iv_locked, iv_stop_count, iv_anime_count, iv_walk_anime,
    iv_step_anime, iv_pattern, iv_original_pattern, iv_move_route_forcing, iv_move_type,
    iv_move_frequency, iv_trigger, iv_interpreter, iv_list, iv_event, iv_display_x, iv_display_y,
    iv_map, iv_events, iv_common_events, iv_animation, iv_character, iv_character_index, iv_cw,
    iv_ch, iv_balloon_duration, iv_balloon_sprite, iv_balloon_id, iv_character_sprites, iv_map_id,
    cv_ani_checker, cv_ani_spr_checker, iv_symbol_encount, iv_wait_count, iv_visibility_distance,
    iv_origin_opacity, iv_opacity, iv_uninhibited_movement, iv_region_z_pos, iv_first_z_pos,
    iv_anti_lag, iv_can_update, iv_data;
static ID id_update_symbol_reaction, id_update_symbol_movement, id_zero, id_truncate, id_between,
    id_reset_sprite_effects, id_is_a, id_MOVE_CONTROL, id_DASH_PLUS, id_MOG_ANTI_LAG,
    id_UPDATE_OUT_SCREEN_RANGE, id_Game_Event;
static ID gv_game_map, gv_data_animations, gv_game_variables, gv_game_system;
static VALUE g_spriteBase = Qnil, g_graphics = Qnil;
static VALUE g_float_256 = Qnil;

#define IV(o, n) ({ static IvSite ivs_; ivFast((o), iv_##n, ivs_); })
#define SETIV(o, n, v) rb_ivar_set((o), iv_##n, (v))

static inline VALUE gameMap() { return rb_gvar_get(gv_game_map); }

static void checkArity(int argc, int lo, int hi) {
    if (argc < lo || argc > hi)
        rb_error_arity(argc, lo, hi);
}

/* ------------------------------------------------------------------ */
/* Game_CharacterBase                                                  */

static VALUE gcb_moving(VALUE self, int, const VALUE *, bool) {
    return rbool(nNe(IV(self, real_x), IV(self, x)) || nNe(IV(self, real_y), IV(self, y)));
}
static VALUE gcb_jumping(VALUE self, int, const VALUE *, bool) {
    return rbool(nGt(IV(self, jump_count), INT2FIX(0)));
}
static VALUE gcb_stopping(VALUE self, int, const VALUE *, bool) {
    return rbool(!RTEST(call0(self, S_GCB_moving)) && !RTEST(call0(self, S_GCB_jumping)));
}
static VALUE gcb_dash(VALUE, int, const VALUE *, bool) { return Qfalse; }
static VALUE gcb_real_move_speed(VALUE self, int, const VALUE *, bool) {
    VALUE speed = IV(self, move_speed);
    return nAdd(speed, INT2FIX(RTEST(call0(self, S_GCB_dash)) ? 1 : 0));
}
static VALUE gcb_distance_per_frame(VALUE self, int, const VALUE *, bool) {
    VALUE s = call0(self, S_GCB_real_move_speed);
    if (FIXNUM_P(s) && FIX2LONG(s) >= 0 && FIX2LONG(s) <= 29)
        return DBL2NUM((double)(1L << FIX2LONG(s)) / 256.0);
    return rb_funcall(rb_funcall(INT2FIX(2), id_pow, 1, s), id_div, 1, g_float_256);
}
static VALUE gcb_object_character(VALUE self, int, const VALUE *, bool) {
    if (nGt(IV(self, tile_id), INT2FIX(0)))
        return Qtrue;
    VALUE name = IV(self, character_name);
    if (RB_TYPE_P(name, T_STRING) && rb_class_of(name) == rb_cString)
        return rbool(RSTRING_LEN(name) > 0 && RSTRING_PTR(name)[0] == '!');
    VALUE first = rb_funcall(name, id_aref, 2, INT2FIX(0), INT2FIX(1));
    return rbool(nEq(first, rb_str_new_cstr("!")));
}
static VALUE gcb_shift_y(VALUE self, int, const VALUE *, bool) {
    return RTEST(call0(self, S_GCB_object_character)) ? INT2FIX(0) : INT2FIX(4);
}
static VALUE gcb_jump_height(VALUE self, int, const VALUE *, bool) {
    VALUE p = IV(self, jump_peak), c = IV(self, jump_count);
    if (FIXNUM_P(p) && FIXNUM_P(c)) {
        long long pp = FIX2LONG(p), cc = FIX2LONG(c);
        long long d = cc - pp;
        if (d < 0) d = -d;
        long long n = pp * pp - d * d;
        long long q = n / 2;
        if ((n % 2 != 0) && n < 0) q -= 1;
        return LL2NUM(q);
    }
    return callOrig(self, S_GCB_jump_height, 0, nullptr);
}
/* Integer/Float values held in C while a position is worked out. Ruby's Float is a heap
 * object on the Vita (no flonum), so each Float step in Ruby allocates; here only the final
 * value becomes a Ruby object, or none when it goes straight to a sprite setter. The operations
 * give Ruby's results: Integer op Integer stays Integer, anything with a Float is a double op. */
struct Num {
    bool f;
    long long i;
    double d;
};
static inline Num NI(long long v) { return Num{false, v, 0.0}; }
static inline double dv(const Num &n) { return n.f ? n.d : (double)n.i; }
static inline bool toNum(VALUE v, Num &n) {
    if (FIXNUM_P(v)) { n = NI(FIX2LONG(v)); return true; }
    if (RB_FLOAT_TYPE_P(v)) { n = Num{true, 0, RFLOAT_VALUE(v)}; return true; }
    return false;
}
static inline VALUE fromNum(const Num &n) { return n.f ? DBL2NUM(n.d) : LL2NUM(n.i); }
static inline Num addN(const Num &a, const Num &b) {
    return (!a.f && !b.f) ? NI(a.i + b.i) : Num{true, 0, dv(a) + dv(b)};
}
static inline Num subN(const Num &a, const Num &b) {
    return (!a.f && !b.f) ? NI(a.i - b.i) : Num{true, 0, dv(a) - dv(b)};
}
static inline Num mulN(const Num &a, const Num &b) {
    return (!a.f && !b.f) ? NI(a.i * b.i) : Num{true, 0, dv(a) * dv(b)};
}
static inline bool ltN(const Num &a, const Num &b) {
    return (!a.f && !b.f) ? a.i < b.i : dv(a) < dv(b);
}
/* Argument for an engine int setter: the setter truncates a Float (NUM2LONG), so a Float in
 * range is passed as that Integer. */
static inline VALUE intArg(const Num &n) {
    if (!n.f)
        return LL2NUM(n.i);
    if (n.d > -1073741824.0 && n.d < 1073741824.0)
        return LONG2FIX((long)n.d);
    return DBL2NUM(n.d);
}

/* Game_Map#adjust_x/adjust_y body (default text), in C values. False: a value is not a plain
 * Integer/Float; the caller then takes the Ruby-value path. */
static bool adjustN(VALUE map, bool horiz, VALUE coord, Num &out) {
    Num x, disp;
    if (!toNum(coord, x) || !toNum(horiz ? IV(map, display_x) : IV(map, display_y), disp))
        return false;
    if (RTEST(call0(map, horiz ? S_GM_loop_horizontal : S_GM_loop_vertical))) {
        VALUE half = nDiv(nSub(call0(map, horiz ? S_GM_width : S_GM_height),
                               call0(map, horiz ? S_GM_screen_tile_x : S_GM_screen_tile_y)),
                          INT2FIX(2));
        Num h, size;
        if (!toNum(half, h))
            return false;
        if (ltN(x, subN(disp, h))) {
            if (!toNum(call0(IV(map, map), horiz ? S_MAP_width : S_MAP_height), size))
                return false;
            out = addN(subN(x, disp), size);
            return true;
        }
    }
    out = subN(x, disp);
    return true;
}
/* $game_map.adjust_x/y(v) as a C value, through the game's own method if it replaced it. */
static bool adjustCall(bool horiz, VALUE coord, Num &out) {
    VALUE map = gameMap();
    Slot &s = g_slots[horiz ? S_GM_adjust_x : S_GM_adjust_y];
    if (slotOk(s, rb_class_of(map)) && s.kind == SK_NATIVE) {
        s.direct++;
        if (adjustN(map, horiz, coord, out))
            return true;
        return toNum(callOrig(map, horiz ? S_GM_adjust_x : S_GM_adjust_y, 1, &coord), out);
    }
    return toNum(call1(map, horiz ? S_GM_adjust_x : S_GM_adjust_y, coord), out);
}
/* Bodies of the default screen_x / screen_y. */
static bool screenXCore(VALUE c, Num &out) {
    Num ax;
    if (!adjustCall(true, IV(c, real_x), ax))
        return false;
    out = addN(mulN(ax, NI(32)), NI(16));
    return true;
}
static bool screenYCore(VALUE c, Num &out) {
    Num ay, sh, jh;
    if (!adjustCall(false, IV(c, real_y), ay))
        return false;
    Num v = addN(mulN(ay, NI(32)), NI(32));
    if (!toNum(call0(c, S_GCB_shift_y), sh))
        return false;
    v = subN(v, sh);
    if (!toNum(call0(c, S_GCB_jump_height), jh))
        return false;
    out = subN(v, jh);
    return true;
}
/* c.screen_x / c.screen_y as a VALUE-free number when they are the defaults. */
static bool screenCall(VALUE c, bool horiz, Num &out, VALUE &boxed, bool *pure = nullptr) {
    Slot &s = g_slots[horiz ? S_GCB_screen_x : S_GCB_screen_y];
    boxed = Qundef;
    if (pure)
        *pure = false;
    if (slotOk(s, rb_class_of(c)) && s.kind == SK_NATIVE) {
        s.direct++;
        if (horiz ? screenXCore(c, out) : screenYCore(c, out)) {
            /* Only default code ran (adjust, shift_y and jump_height read state, change none). */
            if (pure)
                *pure = slotOk(g_slots[S_GM_adjust_x], rb_class_of(gameMap())) &&
                        g_slots[S_GM_adjust_x].kind == SK_NATIVE &&
                        slotOk(g_slots[S_GM_adjust_y], rb_class_of(gameMap())) &&
                        g_slots[S_GM_adjust_y].kind == SK_NATIVE &&
                        slotOk(g_slots[S_GCB_shift_y], rb_class_of(c)) &&
                        slotOk(g_slots[S_GCB_jump_height], rb_class_of(c)) &&
                        slotOk(g_slots[S_GCB_object_character], rb_class_of(c)) &&
                        slotOk(g_slots[S_GM_loop_horizontal], rb_class_of(gameMap())) &&
                        slotOk(g_slots[S_GM_loop_vertical], rb_class_of(gameMap()));
            return true;
        }
    }
    boxed = horiz ? call0(c, S_GCB_screen_x) : call0(c, S_GCB_screen_y);
    return toNum(boxed, out);
}
static VALUE gcb_screen_x(VALUE self, int, const VALUE *, bool) {
    Num n;
    if (screenXCore(self, n))
        return fromNum(n);
    VALUE ax = call1(gameMap(), S_GM_adjust_x, IV(self, real_x));
    return nAdd(nMul(ax, INT2FIX(32)), INT2FIX(16));
}
static VALUE gcb_screen_y(VALUE self, int, const VALUE *, bool) {
    Num n;
    if (screenYCore(self, n))
        return fromNum(n);
    VALUE ay = call1(gameMap(), S_GM_adjust_y, IV(self, real_y));
    VALUE v = nAdd(nMul(ay, INT2FIX(32)), INT2FIX(32));
    v = nSub(v, call0(self, S_GCB_shift_y));
    return nSub(v, call0(self, S_GCB_jump_height));
}
static VALUE gcb_screen_z(VALUE self, int, const VALUE *, bool) {
    return nMul(IV(self, priority_type), INT2FIX(100));
}
static VALUE gcb_normal_priority(VALUE self, int, const VALUE *, bool) {
    return rbool(nEq(IV(self, priority_type), INT2FIX(1)));
}
static VALUE gcb_update_anime_count(VALUE self, int, const VALUE *, bool) {
    if (RTEST(call0(self, S_GCB_moving)) && RTEST(IV(self, walk_anime))) {
        VALUE v = nAdd(IV(self, anime_count), DBL2NUM(1.5));
        SETIV(self, anime_count, v);
        return v;
    }
    if (RTEST(IV(self, step_anime)) || nNe(IV(self, pattern), IV(self, original_pattern))) {
        VALUE v = nAdd(IV(self, anime_count), INT2FIX(1));
        SETIV(self, anime_count, v);
        return v;
    }
    return Qnil;
}
static VALUE gcb_update_anime_pattern(VALUE self, int, const VALUE *, bool) {
    if (!RTEST(IV(self, step_anime)) && nGt(IV(self, stop_count), INT2FIX(0))) {
        VALUE v = IV(self, original_pattern);
        SETIV(self, pattern, v);
        return v;
    }
    VALUE v = nMod(nAdd(IV(self, pattern), INT2FIX(1)), INT2FIX(4));
    SETIV(self, pattern, v);
    return v;
}
static VALUE gcb_update_animation(VALUE self, int, const VALUE *, bool) {
    call0(self, S_GCB_update_anime_count);
    VALUE limit = nSub(INT2FIX(18), nMul(call0(self, S_GCB_real_move_speed), INT2FIX(2)));
    if (nGt(IV(self, anime_count), limit)) {
        call0(self, S_GCB_update_anime_pattern);
        SETIV(self, anime_count, INT2FIX(0));
        return INT2FIX(0);
    }
    return Qnil;
}
static VALUE gcb_update_move(VALUE self, int, const VALUE *, bool) {
    if (nLt(IV(self, x), IV(self, real_x)))
        SETIV(self, real_x, nMax2(nSub(IV(self, real_x), call0(self, S_GCB_distance_per_frame)), IV(self, x)));
    if (nGt(IV(self, x), IV(self, real_x)))
        SETIV(self, real_x, nMin2(nAdd(IV(self, real_x), call0(self, S_GCB_distance_per_frame)), IV(self, x)));
    if (nLt(IV(self, y), IV(self, real_y)))
        SETIV(self, real_y, nMax2(nSub(IV(self, real_y), call0(self, S_GCB_distance_per_frame)), IV(self, y)));
    if (nGt(IV(self, y), IV(self, real_y)))
        SETIV(self, real_y, nMin2(nAdd(IV(self, real_y), call0(self, S_GCB_distance_per_frame)), IV(self, y)));
    if (!RTEST(call0(self, S_GCB_moving)))
        return call0(self, S_GCB_update_bush_depth);
    return Qnil;
}
static VALUE gcb_update_stop(VALUE self, int, const VALUE *, bool) {
    if (RTEST(IV(self, locked)))
        return Qnil;
    VALUE v = nAdd(IV(self, stop_count), INT2FIX(1));
    SETIV(self, stop_count, v);
    return v;
}
static VALUE gcb_update(VALUE self, int, const VALUE *, bool) {
    call0(self, S_GCB_update_animation);
    if (RTEST(call0(self, S_GCB_jumping)))
        return call0(self, S_GCB_update_jump);
    if (RTEST(call0(self, S_GCB_moving)))
        return call0(self, S_GCB_update_move);
    return call0(self, S_GCB_update_stop);
}

/* ------------------------------------------------------------------ */
/* Game_Character / Game_Event                                         */

static VALUE gc_update_stop(VALUE self, int, const VALUE *, bool fromRuby) {
    callSuper(self, S_GC_update_stop, 0, nullptr, fromRuby);
    if (RTEST(IV(self, move_route_forcing)))
        return call0(self, S_GC_update_routine_move);
    return Qnil;
}
static VALUE ge_update_stop(VALUE self, int, const VALUE *, bool fromRuby) {
    callSuper(self, S_GE_update_stop, 0, nullptr, fromRuby);
    if (!RTEST(IV(self, move_route_forcing)))
        return call0(self, S_GE_update_self_movement);
    return Qnil;
}
static VALUE gfxWidth() { return call0(g_graphics, S_GFX_width); }
static VALUE gfxHeight() { return call0(g_graphics, S_GFX_height); }

static VALUE ge_near_the_screen(VALUE self, int argc, const VALUE *argv, bool) {
    checkArity(argc, 0, 2);
    VALUE dx = argc > 0 ? argv[0] : INT2FIX(12);
    VALUE dy = argc > 1 ? argv[1] : INT2FIX(8);
    VALUE gm = gameMap();
    VALUE ax = nSub(call1(gm, S_GM_adjust_x, IV(self, real_x)), nDiv(nDiv(gfxWidth(), INT2FIX(2)), INT2FIX(32)));
    VALUE ay = nSub(call1(gm, S_GM_adjust_y, IV(self, real_y)), nDiv(nDiv(gfxHeight(), INT2FIX(2)), INT2FIX(32)));
    return rbool(nGe(ax, nNeg(dx)) && nLe(ax, dx) && nGe(ay, nNeg(dy)) && nLe(ay, dy));
}
static VALUE ge_stop_count_threshold(VALUE self, int, const VALUE *, bool) {
    return nMul(INT2FIX(30), nSub(INT2FIX(5), IV(self, move_frequency)));
}
static VALUE ge_update_self_movement(VALUE self, int, const VALUE *, bool) {
    if (RTEST(call0(self, S_GE_near_the_screen)) &&
        nGt(IV(self, stop_count), call0(self, S_GE_stop_count_threshold))) {
        VALUE t = IV(self, move_type);
        if (nEq(INT2FIX(1), t)) return call0(self, S_GE_move_type_random);
        if (nEq(INT2FIX(2), t)) return call0(self, S_GE_move_type_toward_player);
        if (nEq(INT2FIX(3), t)) return call0(self, S_GE_move_type_custom);
    }
    return Qnil;
}
static VALUE ge_check_event_trigger_auto(VALUE self, int, const VALUE *, bool) {
    if (nEq(IV(self, trigger), INT2FIX(3)))
        return call0(self, S_GE_start);
    return Qnil;
}
static VALUE ge_update(VALUE self, int, const VALUE *, bool fromRuby) {
    callSuper(self, S_GE_update, 0, nullptr, fromRuby);
    call0(self, S_GE_check_event_trigger_auto);
    VALUE interp = IV(self, interpreter);
    if (!RTEST(interp))
        return Qnil;
    if (!RTEST(rb_funcall(interp, id_running, 0)))
        rb_funcall(interp, id_setup, 2, IV(self, list), call0(IV(self, event), S_EVENT_id));
    return rb_funcall(interp, id_update, 0);
}

/* ------------------------------------------------------------------ */
/* Game_Map                                                            */

static VALUE gm_loop_horizontal(VALUE self, int, const VALUE *, bool) {
    VALUE m = IV(self, map);
    return rbool(nEq(call0(m, S_MAP_scroll_type), INT2FIX(2)) || nEq(call0(m, S_MAP_scroll_type), INT2FIX(3)));
}
static VALUE gm_loop_vertical(VALUE self, int, const VALUE *, bool) {
    VALUE m = IV(self, map);
    return rbool(nEq(call0(m, S_MAP_scroll_type), INT2FIX(1)) || nEq(call0(m, S_MAP_scroll_type), INT2FIX(3)));
}
static VALUE gm_width(VALUE self, int, const VALUE *, bool) { return call0(IV(self, map), S_MAP_width); }
static VALUE gm_height(VALUE self, int, const VALUE *, bool) { return call0(IV(self, map), S_MAP_height); }
static VALUE gm_screen_tile_x(VALUE, int, const VALUE *, bool) { return nDiv(gfxWidth(), INT2FIX(32)); }
static VALUE gm_screen_tile_y(VALUE, int, const VALUE *, bool) { return nDiv(gfxHeight(), INT2FIX(32)); }
static VALUE gm_adjust_x(VALUE self, int argc, const VALUE *argv, bool) {
    checkArity(argc, 1, 1);
    VALUE x = argv[0];
    if (RTEST(call0(self, S_GM_loop_horizontal))) {
        VALUE half = nDiv(nSub(call0(self, S_GM_width), call0(self, S_GM_screen_tile_x)), INT2FIX(2));
        if (nLt(x, nSub(IV(self, display_x), half)))
            return nAdd(nSub(x, IV(self, display_x)), call0(IV(self, map), S_MAP_width));
    }
    return nSub(x, IV(self, display_x));
}
static VALUE gm_adjust_y(VALUE self, int argc, const VALUE *argv, bool) {
    checkArity(argc, 1, 1);
    VALUE y = argv[0];
    if (RTEST(call0(self, S_GM_loop_vertical))) {
        VALUE half = nDiv(nSub(call0(self, S_GM_height), call0(self, S_GM_screen_tile_y)), INT2FIX(2));
        if (nLt(y, nSub(IV(self, display_y), half)))
            return nAdd(nSub(y, IV(self, display_y)), call0(IV(self, map), S_MAP_height));
    }
    return nSub(y, IV(self, display_y));
}
static int updateEachEvent(VALUE, VALUE value, VALUE) {
    call0(value, S_GE_update);
    return ST_CONTINUE;
}
static VALUE gm_update_events(VALUE self, int, const VALUE *, bool) {
    VALUE events = IV(self, events);
    VALUE common = IV(self, common_events);
    /* A Hash and an Array as the default scripts make them; anything else runs the Ruby body. */
    if (!(RB_TYPE_P(events, T_HASH) && rb_class_of(events) == rb_cHash &&
          RB_TYPE_P(common, T_ARRAY) && rb_class_of(common) == rb_cArray))
        return callOrig(self, S_GM_update_events, 0, nullptr);
    rb_hash_foreach(events, updateEachEvent, Qnil);
    /* Array#each reads the length again after every element. */
    for (long i = 0; i < RARRAY_LEN(common); ++i)
        rb_funcall(RARRAY_AREF(common, i), id_update, 0);
    return common;
}

/* ------------------------------------------------------------------ */
/* Sprite_Base / Sprite_Character / Spriteset_Map                      */

static void clearArray(VALUE a) {
    if (RB_TYPE_P(a, T_ARRAY) && rb_class_of(a) == rb_cArray && slotOk(g_slots[S_ARY_clear], rb_cArray))
        rb_ary_clear(a);
    else
        rb_funcall(a, id_clear, 0);
}
static VALUE sb_animation(VALUE self, int, const VALUE *, bool) {
    return rbool(!NIL_P(IV(self, animation)));
}
static VALUE sb_update_animation(VALUE self, int, const VALUE *, bool) {
    if (!RTEST(call0(self, S_SB_animation)))
        return Qnil;
    return callOrig(self, S_SB_update_animation, 0, nullptr);
}
static VALUE sb_update(VALUE self, int, const VALUE *, bool fromRuby) {
    callSuper(self, S_SB_update, 0, nullptr, fromRuby);
    call0(self, S_SB_update_animation);
    clearArray(rb_cvar_get(g_spriteBase, cv_ani_checker));
    VALUE b = rb_cvar_get(g_spriteBase, cv_ani_spr_checker);
    clearArray(b);
    return b;
}
static VALUE sc_graphic_changed(VALUE self, int, const VALUE *, bool) {
    VALUE c = IV(self, character);
    return rbool(nNe(IV(self, tile_id), call0(c, S_GCB_tile_id)) ||
                 nNe(IV(self, character_name), call0(c, S_GCB_character_name)) ||
                 nNe(IV(self, character_index), call0(c, S_GCB_character_index)));
}
static VALUE sc_update_bitmap(VALUE self, int, const VALUE *, bool) {
    if (!RTEST(call0(self, S_SC_graphic_changed)))
        return Qnil;
    VALUE c = IV(self, character);
    SETIV(self, tile_id, call0(c, S_GCB_tile_id));
    SETIV(self, character_name, call0(c, S_GCB_character_name));
    SETIV(self, character_index, call0(c, S_GCB_character_index));
    if (nGt(IV(self, tile_id), INT2FIX(0)))
        return call0(self, S_SC_set_tile_bitmap);
    return call0(self, S_SC_set_character_bitmap);
}
static VALUE sc_update_src_rect(VALUE self, int, const VALUE *, bool) {
    if (!nEq(IV(self, tile_id), INT2FIX(0)))
        return Qnil;
    VALUE c = IV(self, character);
    VALUE index = call0(c, S_GCB_character_index);
    VALUE pattern = nLt(call0(c, S_GCB_pattern), INT2FIX(3)) ? call0(c, S_GCB_pattern) : INT2FIX(1);
    VALUE sx = nMul(nAdd(nMul(nMod(index, INT2FIX(4)), INT2FIX(3)), pattern), IV(self, cw));
    VALUE sy = nMul(nAdd(nMul(nDiv(index, INT2FIX(4)), INT2FIX(4)),
                         nDiv(nSub(call0(c, S_GCB_direction), INT2FIX(2)), INT2FIX(2))),
                    IV(self, ch));
    VALUE args[4] = {sx, sy, IV(self, cw), IV(self, ch)};
    /* Sprite#src_rect and Rect#set as the engine does them: the sprite's hidden src_rect
     * object, then Rect::set with the four Integers (the binding's "iiii" conversion). */
    Slot &g = g_slots[S_SPR_src_rect], &st = g_slots[S_RECT_set];
    Sprite *sp = getPrivateDataNoRaise<Sprite>(self);
    if (sp && !sp->isDisposed() && slotOk(g, rb_class_of(self)) && g.kind == SK_ENGINE) {
        g.direct++;
        VALUE rect = rb_ivar_get(self, id_src_rect_iv);
        if (FIXNUM_P(args[0]) && FIXNUM_P(args[1]) && FIXNUM_P(args[2]) && FIXNUM_P(args[3]) &&
            !RB_SPECIAL_CONST_P(rect) && slotOk(st, rb_class_of(rect)) && st.kind == SK_ENGINE) {
            Rect *r = getPrivateDataNoRaise<Rect>(rect);
            if (r) {
                st.direct++;
                r->set(FIX2INT(args[0]), FIX2INT(args[1]), FIX2INT(args[2]), FIX2INT(args[3]));
                return rect;
            }
        }
        return callSlot(rect, S_RECT_set, 4, args);
    }
    VALUE rect = call0(self, S_SPR_src_rect);
    return callSlot(rect, S_RECT_set, 4, args);
}
/* Sprite setter from a C value; through Ruby (boxed) if the screen value was not plain. */
static inline void setFromNum(VALUE self, int slot, bool ok, const Num &n, VALUE boxed) {
    call1(self, slot, ok ? intArg(n) : boxed);
}
static VALUE sc_update_position_jp(VALUE self, int, const VALUE *, bool) {
    VALUE c = IV(self, character);
    Num n;
    VALUE boxed;
    bool ok = screenCall(c, true, n, boxed);
    setFromNum(self, S_SPR_set_x, ok, n, boxed);
    ok = screenCall(c, false, n, boxed);
    setFromNum(self, S_SPR_set_y, ok, n, boxed);
    VALUE z = call0(c, S_GCB_screen_z);
    call1(self, S_SPR_set_z, z);
    return z;
}
/* move_animation(screen_x - x, screen_y - y): the default move_animation does nothing without
 * an animation, so the differences only become Ruby values when they are used. Returns true
 * when only default code ran (no animation, default screen_x/screen_y), with the two screen
 * values in sx/sy: then nothing changed and update_position may use them again. */
static bool moveAnimationBy(VALUE self, VALUE c, Num &sx, Num &sy) {
    Num x, y;
    VALUE bx, by;
    bool px, py;
    bool okx = screenCall(c, true, sx, bx, &px);
    VALUE vx = call0(self, S_SPR_x);
    bool oky = screenCall(c, false, sy, by, &py);
    VALUE vy = call0(self, S_SPR_y);
    Slot &m = g_slots[S_SC_move_animation];
    if (slotOk(m, rb_class_of(self)) && m.kind == SK_NATIVE && !RTEST(IV(self, animation))) {
        m.direct++;
        return okx && oky && px && py && slotOk(g_slots[S_SPR_x], rb_class_of(self)) &&
               slotOk(g_slots[S_SPR_y], rb_class_of(self));
    }
    VALUE dx = (okx && toNum(vx, x)) ? fromNum(subN(sx, x)) : nSub(okx ? fromNum(sx) : bx, vx);
    VALUE dy = (oky && toNum(vy, y)) ? fromNum(subN(sy, y)) : nSub(oky ? fromNum(sy) : by, vy);
    call2(self, S_SC_move_animation, dx, dy);
    return false;
}
/* The Japanese default body with screen_x/screen_y already worked out. */
static VALUE positionWith(VALUE self, VALUE c, const Num &sx, const Num &sy) {
    call1(self, S_SPR_set_x, intArg(sx));
    call1(self, S_SPR_set_y, intArg(sy));
    VALUE z = call0(c, S_GCB_screen_z);
    call1(self, S_SPR_set_z, z);
    return z;
}
static VALUE sc_update_position_en(VALUE self, int, const VALUE *, bool) {
    VALUE c = IV(self, character);
    Num sx, sy;
    if (moveAnimationBy(self, c, sx, sy))
        return positionWith(self, c, sx, sy);
    return sc_update_position_jp(self, 0, nullptr, false);
}
/* Enterbrain's VXAce_SP1 patch: aliases the default (Japanese) update_position as
 * vxace_sp1_update_position and wraps it with move_animation. */
static VALUE sc_update_position_sp1(VALUE self, int, const VALUE *, bool) {
    VALUE c = IV(self, character);
    Num sx, sy;
    bool same = moveAnimationBy(self, c, sx, sy);
    Slot &h = g_slots[S_SC_sp1_orig];
    if (same && slotOk(h, rb_class_of(self)) && h.kind == SK_NATIVE) {
        h.direct++;
        return positionWith(self, c, sx, sy);
    }
    return call0(self, S_SC_sp1_orig);
}
static VALUE sc_move_animation(VALUE self, int argc, const VALUE *argv, bool) {
    checkArity(argc, 2, 2);
    if (!RTEST(IV(self, animation)))
        return Qnil;
    return callOrig(self, S_SC_move_animation, argc, argv);
}
static VALUE sc_update_other(VALUE self, int, const VALUE *, bool) {
    VALUE c = IV(self, character);
    call1(self, S_SPR_set_opacity, call0(c, S_GCB_opacity));
    call1(self, S_SPR_set_blend_type, call0(c, S_GCB_blend_type));
    call1(self, S_SPR_set_bush_depth, call0(c, S_GCB_bush_depth));
    VALUE v = rbool(!RTEST(call0(c, S_GCB_transparent)));
    call1(self, S_SPR_set_visible, v);
    return v;
}
static VALUE sc_update_balloon(VALUE self, int, const VALUE *, bool) {
    if (!nGt(IV(self, balloon_duration), INT2FIX(0)))
        return Qnil;
    return callOrig(self, S_SC_update_balloon, 0, nullptr);
}
static VALUE sc_setup_new_effect(VALUE self, int, const VALUE *, bool) {
    VALUE c = IV(self, character);
    if (!RTEST(call0(self, S_SB_animation)) && nGt(call0(c, S_GCB_animation_id), INT2FIX(0))) {
        VALUE anim = rb_funcall(rb_gvar_get(gv_data_animations), id_aref, 1, call0(c, S_GCB_animation_id));
        call1(self, S_SC_start_animation, anim);
    }
    if (!RTEST(IV(self, balloon_sprite)) && nGt(call0(c, S_GCB_balloon_id), INT2FIX(0))) {
        SETIV(self, balloon_id, call0(c, S_GCB_balloon_id));
        return call0(self, S_SC_start_balloon);
    }
    return Qnil;
}
static VALUE sc_update(VALUE self, int, const VALUE *, bool fromRuby) {
    callSuper(self, S_SC_update, 0, nullptr, fromRuby);
    call0(self, S_SC_update_bitmap);
    call0(self, S_SC_update_src_rect);
    call0(self, S_SC_update_position);
    call0(self, S_SC_update_other);
    call0(self, S_SC_update_balloon);
    return call0(self, S_SC_setup_new_effect);
}
static VALUE sm_update_characters(VALUE self, int, const VALUE *, bool) {
    if (nNe(IV(self, map_id), rb_funcall(gameMap(), rb_intern("map_id"), 0)))
        call0(self, S_SM_refresh_characters);
    VALUE list = IV(self, character_sprites);
    if (RB_TYPE_P(list, T_ARRAY) && rb_class_of(list) == rb_cArray) {
        for (long i = 0; i < RARRAY_LEN(list); ++i)
            call0(RARRAY_AREF(list, i), S_SC_update);
        return list;
    }
    return callOrig(self, S_SM_update_characters, 0, nullptr);
}


/* ------------------------------------------------------------------ */
/* Popular scripts                                                     */
/*                                                                     */
/* Each native below matches one exact script text (see STOCK in       */
/* launcher/native_events.rb). A wrapper (`alias old new; def new ...   */
/* old ... end`) calls the method it aliased through a helper slot,    */
/* which runs the native that alias points to while the alias still    */
/* resolves to it. Rare branches run the script's own Ruby body.        */

/* Call `name` on self through whichever of two slots (same name, different owners) the
 * object's class resolves to; through Ruby when neither. */
static VALUE callEither(VALUE self, int a, int b) {
    VALUE k = rb_class_of(self);
    for (int id : {a, b}) {
        Slot &s = g_slots[id];
        if (s.kind == SK_NATIVE && slotOk(s, k)) {
            s.direct++;
            return s.native(self, 0, nullptr, false);
        }
    }
    g_slots[a].viaRuby++;
    return rb_funcall(self, g_slots[a].mid, 0);
}
/* A builtin method still in place for this receiver's class (checked, never called). */
static inline bool builtinOk(int id, VALUE recv) {
    Slot &s = g_slots[id];
    return s.kind != SK_NONE && slotOk(s, rb_class_of(recv));
}
static VALUE truncV(VALUE v) {
    if (FIXNUM_P(v) && builtinOk(S_INT_truncate, v))
        return v;
    if (isFlo(v) && builtinOk(S_FLO_truncate, v)) {
        double d = RFLOAT_VALUE(v);
        if (std::isfinite(d) && std::fabs(d) < 1e9)
            return LONG2FIX((long)std::trunc(d));
    }
    return rb_funcall(v, id_truncate, 0);
}
static bool betweenV(VALUE v, VALUE lo, VALUE hi) {
    if (FIXNUM_P(v) && FIXNUM_P(lo) && FIXNUM_P(hi) && builtinOk(S_INT_between, v))
        return FIX2LONG(lo) <= FIX2LONG(v) && FIX2LONG(v) <= FIX2LONG(hi);
    return RTEST(rb_funcall(v, id_between, 2, lo, hi));
}
static bool isA(VALUE obj, VALUE klass) {
    if (builtinOk(S_OBJ_is_a, obj))
        return RTEST(rb_obj_is_kind_of(obj, klass));
    return RTEST(rb_funcall(obj, id_is_a, 1, klass));
}

/* -- symbol encounter (シンボルエンカウント補助) -- */

/* Game_Character: alias _symbol_opacity_update update
 *   def update; _symbol_opacity_update; update_symbol_opacity; end */
static VALUE gc_update_symbol(VALUE self, int, const VALUE *, bool) {
    call0(self, S_H_GC_update_symbol);
    return callEither(self, S_GE_update_symbol_opacity, S_GC_update_symbol_opacity);
}
/* Game_Character#update_symbol_opacity: empty. */
static VALUE gc_update_symbol_opacity(VALUE, int, const VALUE *, bool) { return Qnil; }
/* Game_Event#update_symbol_opacity */
static VALUE ge_update_symbol_opacity(VALUE self, int, const VALUE *, bool) {
    VALUE vd = IV(self, visibility_distance);
    bool zero = FIXNUM_P(vd) && builtinOk(S_INT_zero, vd) ? vd == INT2FIX(0) : RTEST(rb_funcall(vd, id_zero, 0));
    if (!zero)
        return callOrig(self, S_GE_update_symbol_opacity, 0, nullptr);
    VALUE o = IV(self, origin_opacity);
    SETIV(self, opacity, o);
    return o;
}
/* Game_Event: alias _update_with_symbol update
 *   def update
 *     update_symbol_reaction if @symbol_encount && @wait_count <= 0 && !@move_route_forcing
 *     _update_with_symbol
 *   end */
static VALUE ge_update_symbol(VALUE self, int, const VALUE *, bool) {
    if (RTEST(IV(self, symbol_encount)) && nLe(IV(self, wait_count), INT2FIX(0)) &&
        !RTEST(IV(self, move_route_forcing)))
        rb_funcall(self, id_update_symbol_reaction, 0);
    return call0(self, S_H_GE_update_symbol);
}
/* Game_Event: alias _update_self_movement_with_symbol update_self_movement
 *   def update_self_movement
 *     if @symbol_encount
 *       update_symbol_movement if near_the_screen? && @stop_count > stop_count_threshold
 *     else
 *       _update_self_movement_with_symbol
 *     end
 *   end */
static VALUE ge_update_self_movement_symbol(VALUE self, int, const VALUE *, bool) {
    if (RTEST(IV(self, symbol_encount))) {
        if (RTEST(call0(self, S_GE_near_the_screen)) &&
            nGt(IV(self, stop_count), call0(self, S_GE_stop_count_threshold)))
            return rb_funcall(self, id_update_symbol_movement, 0);
        return Qnil;
    }
    return call0(self, S_H_GE_usm_symbol);
}

/* -- CACAO off-screen movement (画面外の自律移動) -- */

static VALUE ge_update_self_movement_cacao(VALUE self, int, const VALUE *, bool) {
    if (RTEST(call0(self, S_GE_self_movable))) {
        VALUE t = IV(self, move_type);
        if (nEq(INT2FIX(1), t)) return call0(self, S_GE_move_type_random);
        if (nEq(INT2FIX(2), t)) return call0(self, S_GE_move_type_toward_player);
        if (nEq(INT2FIX(3), t)) return call0(self, S_GE_move_type_custom);
    }
    return Qnil;
}
static VALUE ge_self_movable_cacao(VALUE self, int, const VALUE *, bool) {
    if (!(RTEST(IV(self, uninhibited_movement)) || RTEST(call0(self, S_GE_near_the_screen))))
        return Qfalse;
    if (!nGt(IV(self, stop_count), call0(self, S_GE_stop_count_threshold)))
        return Qfalse;
    return Qtrue;
}

/* -- 8-direction movement (星潟): real_move_speed with MOVE_CONTROL::DASH_PLUS -- */

/* Game_Variables#[]: @data[variable_id] || 0 */
static VALUE gv_aref(VALUE self, int argc, const VALUE *argv, bool) {
    checkArity(argc, 1, 1);
    VALUE d = IV(self, data), v;
    if (FIXNUM_P(argv[0]) && RB_TYPE_P(d, T_ARRAY) && rb_class_of(d) == rb_cArray && builtinOk(S_ARY_aref, d))
        v = rb_ary_entry(d, FIX2LONG(argv[0]));
    else
        v = rb_funcall(d, id_aref, 1, argv[0]);
    return RTEST(v) ? v : INT2FIX(0);
}
/* Game_CharacterBase: alias real_move_speed_8direction real_move_speed
 *   def real_move_speed
 *     if $game_variables[MOVE_CONTROL::DASH_PLUS] > 0 ... else real_move_speed_8direction end
 *   end */
static VALUE gcb_real_move_speed_8dir(VALUE self, int, const VALUE *, bool) {
    VALUE mc = rb_const_get(g_slots[S_GCB_real_move_speed].owner, id_MOVE_CONTROL);
    VALUE k = rb_const_get_from(mc, id_DASH_PLUS);
    VALUE v = call1(rb_gvar_get(gv_game_variables), S_GV_aref, k);
    if (nGt(v, INT2FIX(0)))
        return callOrig(self, S_H_GCB_rms_8dir, 0, nullptr); /* the script's own body */
    return call0(self, S_H_GCB_rms_8dir);
}

/* -- kure region pass (リージョン通行設定): screen_z -- */

static VALUE gcb_screen_z_kure(VALUE self, int, const VALUE *, bool) {
    VALUE v = IV(self, region_z_pos);
    if (RTEST(v))
        return v;
    v = IV(self, first_z_pos);
    if (RTEST(v))
        return v;
    return nMul(IV(self, priority_type), INT2FIX(100));
}

/* -- MOG Anti Lag -- */

/* Game_Event: alias mog_anti_lag_update update
 *   def update; update_anti_lag unless !@anti_lag; return if !@can_update; mog_anti_lag_update; end */
static VALUE ge_update_mog(VALUE self, int, const VALUE *, bool) {
    if (RTEST(IV(self, anti_lag)))
        call0(self, S_GE_update_anti_lag);
    if (!RTEST(IV(self, can_update)))
        return Qnil;
    return call0(self, S_H_GE_update_mog);
}
static VALUE ge_update_anti_lag(VALUE self, int, const VALUE *, bool) {
    if (!RTEST(call0(rb_gvar_get(gv_game_system), S_GS_anti_lag))) {
        SETIV(self, can_update, Qtrue);
        return Qnil;
    }
    return call0(self, S_GE_anti_lag_on_screen);
}
static VALUE ge_anti_lag_event_on_screen(VALUE self, int, const VALUE *, bool) {
    SETIV(self, can_update, Qfalse);
    VALUE mod = rb_const_get(g_slots[S_GE_anti_lag_on_screen].owner, id_MOG_ANTI_LAG);
    VALUE out = rb_const_get_from(mod, id_UPDATE_OUT_SCREEN_RANGE);
    VALUE gm = gameMap();
    VALUE px = truncV(call0(gm, S_GM_display_x));
    VALUE py = truncV(call0(gm, S_GM_display_y));
    VALUE dx = nSub(IV(self, x), px);
    VALUE dy = nSub(IV(self, y), py);
    if (betweenV(dx, nSub(INT2FIX(0), out), nAdd(INT2FIX(20), out)) &&
        betweenV(dy, nSub(INT2FIX(0), out), nAdd(INT2FIX(15), out))) {
        SETIV(self, can_update, Qtrue);
        return Qtrue;
    }
    return Qnil;
}
static VALUE sc_check_can_update_sprite(VALUE self, int, const VALUE *, bool) {
    VALUE c = IV(self, character);
    if (RTEST(call0(self, S_SPR_visible)) && !RTEST(call0(c, S_GE_can_update)))
        rb_funcall(self, id_reset_sprite_effects, 0);
    VALUE v = call0(c, S_GE_can_update);
    call1(self, S_SPR_set_visible, v);
    return v;
}
/* Sprite_Character: alias mog_anti_lag_update update
 *   def update
 *     if $game_system.anti_lag and @character.is_a?(Game_Event)
 *       check_can_update_sprite
 *       return unless self.visible
 *     end
 *     mog_anti_lag_update
 *   end */
static VALUE sc_update_mog(VALUE self, int, const VALUE *, bool) {
    if (RTEST(call0(rb_gvar_get(gv_game_system), S_GS_anti_lag)) &&
        isA(IV(self, character), rb_const_get(g_slots[S_SC_update].owner, id_Game_Event))) {
        call0(self, S_SC_check_can_update_sprite);
        if (!RTEST(call0(self, S_SPR_visible)))
            return Qnil;
    }
    return call0(self, S_H_SC_update_mog);
}

/* ------------------------------------------------------------------ */
/* Ruby-facing entry points                                            */

struct NativeDef {
    int slot;
    const char *variant; /* which script text this native matches */
    NativeFn fn;
    int superSlot;       /* slot `super` must reach, -1 none, KEEP: a wrapper keeps the slot's */
    int superAlt;        /* another slot `super` may reach instead, or -1 */
    int lo, hi;          /* arity */
    int helper;          /* wrapper: slot of the alias name it calls, or -1 */
    int base;            /* wrapper: slot whose native that alias points to */
};
enum { KEEP = -2 };

#define ND(slot, var, fn, sup) {slot, var, fn, sup, -1, 0, 0, -1, -1}
#define NDS(slot, var, fn, sup, alt) {slot, var, fn, sup, alt, 0, 0, -1, -1}
#define NDA(slot, var, fn, sup, lo, hi) {slot, var, fn, sup, -1, lo, hi, -1, -1}
#define NDW(slot, var, fn, helper, base) {slot, var, fn, KEEP, -1, 0, 0, helper, base}
static const NativeDef g_natives[] = {
    ND(S_GCB_moving, "", gcb_moving, -1),
    ND(S_GCB_jumping, "", gcb_jumping, -1),
    ND(S_GCB_stopping, "", gcb_stopping, -1),
    ND(S_GCB_real_move_speed, "", gcb_real_move_speed, -1),
    ND(S_GCB_distance_per_frame, "", gcb_distance_per_frame, -1),
    ND(S_GCB_dash, "", gcb_dash, -1),
    ND(S_GCB_object_character, "", gcb_object_character, -1),
    ND(S_GCB_shift_y, "", gcb_shift_y, -1),
    ND(S_GCB_jump_height, "", gcb_jump_height, -1),
    ND(S_GCB_screen_x, "", gcb_screen_x, -1),
    ND(S_GCB_screen_y, "", gcb_screen_y, -1),
    ND(S_GCB_screen_z, "", gcb_screen_z, -1),
    ND(S_GCB_normal_priority, "", gcb_normal_priority, -1),
    ND(S_GCB_update, "", gcb_update, -1),
    ND(S_GCB_update_move, "", gcb_update_move, -1),
    ND(S_GCB_update_stop, "", gcb_update_stop, -1),
    ND(S_GCB_update_animation, "", gcb_update_animation, -1),
    ND(S_GCB_update_anime_count, "", gcb_update_anime_count, -1),
    ND(S_GCB_update_anime_pattern, "", gcb_update_anime_pattern, -1),
    ND(S_GC_update_stop, "", gc_update_stop, S_GCB_update_stop),
    /* With the symbol encounter script, `super` from Game_Event#update reaches its
     * Game_Character#update wrapper instead of Game_CharacterBase#update. */
    NDS(S_GE_update, "", ge_update, S_GCB_update, S_GC_update),
    ND(S_GE_update_stop, "", ge_update_stop, S_GC_update_stop),
    ND(S_GE_update_self_movement, "", ge_update_self_movement, -1),
    NDA(S_GE_near_the_screen, "", ge_near_the_screen, -1, 0, 2),
    ND(S_GE_stop_count_threshold, "", ge_stop_count_threshold, -1),
    ND(S_GE_check_event_trigger_auto, "", ge_check_event_trigger_auto, -1),
    NDA(S_GM_adjust_x, "", gm_adjust_x, -1, 1, 1),
    NDA(S_GM_adjust_y, "", gm_adjust_y, -1, 1, 1),
    ND(S_GM_loop_horizontal, "", gm_loop_horizontal, -1),
    ND(S_GM_loop_vertical, "", gm_loop_vertical, -1),
    ND(S_GM_width, "", gm_width, -1),
    ND(S_GM_height, "", gm_height, -1),
    ND(S_GM_screen_tile_x, "", gm_screen_tile_x, -1),
    ND(S_GM_screen_tile_y, "", gm_screen_tile_y, -1),
    ND(S_GM_update_events, "", gm_update_events, -1),
    ND(S_SB_update, "", sb_update, S_SPR_update),
    ND(S_SB_animation, "", sb_animation, -1),
    ND(S_SB_update_animation, "", sb_update_animation, -1),
    ND(S_SC_update, "", sc_update, S_SB_update),
    ND(S_SC_update_bitmap, "", sc_update_bitmap, -1),
    ND(S_SC_graphic_changed, "", sc_graphic_changed, -1),
    ND(S_SC_update_src_rect, "", sc_update_src_rect, -1),
    ND(S_SC_update_position, "jp", sc_update_position_jp, -1),
    ND(S_SC_update_position, "en", sc_update_position_en, -1),
    /* Enterbrain's VXAce_SP1 wraps the Japanese default (alias vxace_sp1_update_position). */
    NDW(S_SC_update_position, "sp1", sc_update_position_sp1, S_SC_sp1_orig, S_SC_update_position),
    ND(S_SC_update_other, "", sc_update_other, -1),
    ND(S_SC_update_balloon, "", sc_update_balloon, -1),
    ND(S_SC_setup_new_effect, "", sc_setup_new_effect, -1),
    NDA(S_SC_move_animation, "", sc_move_animation, -1, 2, 2),
    ND(S_SM_update_characters, "", sm_update_characters, -1),
    /* symbol encounter */
    NDW(S_GC_update, "symbol", gc_update_symbol, S_H_GC_update_symbol, S_GCB_update),
    ND(S_GC_update_symbol_opacity, "symbol", gc_update_symbol_opacity, -1),
    ND(S_GE_update_symbol_opacity, "symbol", ge_update_symbol_opacity, -1),
    NDW(S_GE_update, "symbol", ge_update_symbol, S_H_GE_update_symbol, S_GE_update),
    NDW(S_GE_update_self_movement, "symbol", ge_update_self_movement_symbol, S_H_GE_usm_symbol,
        S_GE_update_self_movement),
    /* CACAO */
    ND(S_GE_update_self_movement, "cacao", ge_update_self_movement_cacao, -1),
    ND(S_GE_self_movable, "cacao", ge_self_movable_cacao, -1),
    /* 8-direction */
    NDA(S_GV_aref, "", gv_aref, -1, 1, 1),
    NDW(S_GCB_real_move_speed, "8dir", gcb_real_move_speed_8dir, S_H_GCB_rms_8dir, S_GCB_real_move_speed),
    /* kure */
    ND(S_GCB_screen_z, "kure", gcb_screen_z_kure, -1),
    /* MOG Anti Lag */
    NDW(S_GE_update, "mog", ge_update_mog, S_H_GE_update_mog, S_GE_update),
    ND(S_GE_update_anti_lag, "mog", ge_update_anti_lag, -1),
    ND(S_GE_anti_lag_on_screen, "mog", ge_anti_lag_event_on_screen, -1),
    ND(S_SC_check_can_update_sprite, "mog", sc_check_can_update_sprite, -1),
    NDW(S_SC_update, "mog", sc_update_mog, S_H_SC_update_mog, S_SC_update),
};
#undef ND
#undef NDS
#undef NDA
#undef NDW
static constexpr int N_NATIVES = sizeof(g_natives) / sizeof(g_natives[0]);

/* The Ruby method of each native: bound to that native, so an alias a script makes of it
 * keeps running it after a wrapper takes over the slot. */
template <int I>
static VALUE entry(int argc, VALUE *argv, VALUE self) {
    const NativeDef &d = g_natives[I];
    checkArity(argc, d.lo, d.hi);
    return d.fn(self, argc, argv, true);
}
template <size_t... Is>
static std::array<RubyMethod, sizeof...(Is)> makeEntries(std::index_sequence<Is...>) {
    return {{entry<(int)Is>...}};
}
static const std::array<RubyMethod, N_NATIVES> g_entries = makeEntries(std::make_index_sequence<N_NATIVES>{});

static int slotByName(const char *owner, const char *name) {
    for (int i = 0; i < S_COUNT; ++i)
        if (g_slots[i].owner_name && !strcmp(g_slots[i].owner_name, owner) && !strcmp(g_slots[i].name, name))
            return i;
    return -1;
}

static VALUE instanceMethod(VALUE klass, ID mid) {
    return rb_funcall(klass, id_instance_method, 1, ID2SYM(mid));
}

/* HardRPGNative.replace(klass, :name, variant): swap in the native method. A wrapper variant
 * also takes the alias it calls (helper slot), which must point to the base slot's native. */
static VALUE nev_replace(VALUE, VALUE klass, VALUE name, VALUE variant) {
    const char *cname = rb_class2name(klass);
    const char *mname = rb_id2name(SYM2ID(name));
    const char *var = StringValueCStr(variant);
    int slot = slotByName(cname, mname);
    if (slot < 0)
        return Qfalse;
    for (int i = 0; i < N_NATIVES; ++i) {
        const NativeDef &d = g_natives[i];
        if (d.slot != slot || strcmp(d.variant, var))
            continue;
        Slot &s = g_slots[slot];
        if (d.helper >= 0) {
            Slot &b = g_slots[d.base], &h = g_slots[d.helper];
            if (b.kind != SK_NATIVE || h.kind != SK_NONE)
                return Qfalse;
            VALUE um = instanceMethod(klass, h.mid);
            h.kind = SK_NATIVE;
            h.native = b.native;
            h.superSlot = -1;
            h.superFrom = b.superSlot >= 0 ? d.base : -1;
            h.owner = rb_funcall(um, rb_intern("owner"), 0);
            h.expect = instanceMethod(h.owner, h.mid);
            rb_gc_register_mark_object(h.expect);
            /* The script's own Ruby body, for the natives that fall back to it. */
            g_orig[d.helper] = rb_intern((std::string("__nev_") + var + "_" + s.name).c_str());
            rb_alias(klass, g_orig[d.helper], s.mid);
            rb_funcall(klass, rb_intern("send"), 2, ID2SYM(rb_intern("private")), ID2SYM(g_orig[d.helper]));
        } else {
            g_orig[slot] = origId(slot);
            rb_alias(klass, g_orig[slot], s.mid);
            rb_funcall(klass, rb_intern("send"), 2, ID2SYM(rb_intern("private")), ID2SYM(g_orig[slot]));
        }
        rb_define_method(klass, mname, RUBY_METHOD_FUNC(g_entries[i]), -1);
        s.kind = SK_NATIVE;
        s.native = d.fn;
        if (d.superSlot != KEEP) {
            s.superSlot = d.superSlot;
            s.superAlt = d.superAlt;
        }
        s.owner = klass;
        s.expect = instanceMethod(klass, s.mid);
        rb_gc_register_mark_object(s.expect);
        g_epoch++;
        return Qtrue;
    }
    return Qfalse;
}

/* HardRPGNative.attr(klass, :name): the stock attr_reader may be read directly. */
static VALUE nev_attr(VALUE, VALUE klass, VALUE name) {
    int slot = slotByName(rb_class2name(klass), rb_id2name(SYM2ID(name)));
    if (slot < 0)
        return Qfalse;
    Slot &s = g_slots[slot];
    s.kind = SK_ATTR;
    s.ivar = rb_intern((std::string("@") + s.name).c_str());
    s.owner = klass;
    s.expect = instanceMethod(klass, s.mid);
    rb_gc_register_mark_object(s.expect);
    g_epoch++;
    return Qtrue;
}

static VALUE nev_bump(VALUE) {
    g_epoch++;
    return Qnil;
}

static VALUE nev_stats(VALUE) {
    VALUE h = rb_hash_new();
    for (int i = 0; i < S_COUNT; ++i) {
        Slot &s = g_slots[i];
        if (!s.owner_name || (!s.direct && !s.viaRuby && s.kind == SK_NONE))
            continue;
        std::string k = std::string(s.owner_name) + "#" + s.name;
        const char *kind = s.kind == SK_NATIVE ? "native" : s.kind == SK_ATTR ? "attr" : s.kind == SK_ENGINE ? "engine" : "ruby";
        rb_hash_aset(h, rb_str_new_cstr(k.c_str()),
                     rb_ary_new_from_args(3, rb_str_new_cstr(kind), UINT2NUM(s.direct), UINT2NUM(s.viaRuby)));
    }
    return h;
}
static VALUE nev_reset_stats(VALUE) {
    for (int i = 0; i < S_COUNT; ++i)
        g_slots[i].direct = g_slots[i].viaRuby = 0;
    return Qnil;
}
static VALUE nev_epoch(VALUE) { return UINT2NUM(g_epoch); }
static VALUE nev_checks(VALUE) { return UINT2NUM(g_checks); }

/* Ruby helpers kept in the engine: resolution checks and the change hooks. */
static const char *kHelpers = R"RUBY(
module HardRPGNative
  # klass resolves name to owner's method, and owner's method is still `expect`.
  def self.resolves_to?(klass, name, owner, expect)
    um = klass.instance_method(name)
    um.owner.equal?(owner) && owner.instance_method(name) == expect
  rescue NameError
    false
  end
  # `super` from owner#name reaches expect_owner's method `expect`.
  def self.super_ok?(owner, name, expect_owner, expect)
    anc = owner.ancestors
    i = anc.index(owner) or return false
    anc[(i + 1)..-1].each do |mod|
      if mod.method_defined?(name, false) || mod.private_method_defined?(name, false)
        return mod.equal?(expect_owner) && mod.instance_method(name) == expect
      end
    end
    false
  rescue StandardError
    false
  end
  HOOK = Module.new do
    def method_added(n); HardRPGNative.bump; super; end
    def method_removed(n); HardRPGNative.bump; super; end
    def method_undefined(n); HardRPGNative.bump; super; end
    def append_features(m); HardRPGNative.bump; super; end
    def prepend_features(m); HardRPGNative.bump; super; end
    def extend_object(o); HardRPGNative.bump; super; end
  end
  OBJ_HOOK = Module.new do
    def singleton_method_added(n); HardRPGNative.bump; super; end
  end
end
Module.prepend(HardRPGNative::HOOK)
Object.prepend(HardRPGNative::OBJ_HOOK)
)RUBY";

static void engineSlot(int id, VALUE klass) {
    Slot &s = g_slots[id];
    RubyMethod fn = engineMethod(klass, s.mid);
    if (!fn)
        return;
    s.kind = SK_ENGINE;
    s.engine = fn;
    s.owner = klass;
    s.expect = instanceMethod(klass, s.mid);
    rb_gc_register_mark_object(s.expect);
}

static VALUE rpgClass(const char *path) {
    return rb_path2class(path);
}

static void attrSlotFrom(int id, VALUE klass) {
    Slot &s = g_slots[id];
    s.kind = SK_ATTR;
    s.ivar = rb_intern((std::string("@") + s.name).c_str());
    s.owner = klass;
    s.expect = instanceMethod(klass, s.mid);
    rb_gc_register_mark_object(s.expect);
}

static VALUE initRuby(VALUE);

void nativeEventsInit(int rgssVersion) {
    if (rgssVersion != 3)
        return;

#define I(v, s) v = rb_intern(s)
    I(id_resolves, "resolves_to?"); I(id_super_ok, "super_ok?"); I(id_instance_method, "instance_method");
    I(id_plus, "+"); I(id_minus, "-"); I(id_mul, "*"); I(id_div, "/"); I(id_mod, "%"); I(id_lt, "<");
    I(id_gt, ">"); I(id_le, "<="); I(id_ge, ">="); I(id_eq, "=="); I(id_neq, "!="); I(id_uminus, "-@");
    I(id_pow, "**"); I(id_aref, "[]"); I(id_max, "max"); I(id_min, "min"); I(id_running, "running?");
    I(id_setup, "setup"); I(id_update, "update"); I(id_clear, "clear"); I(id_abs, "abs");
    I(iv_real_x, "@real_x"); I(iv_real_y, "@real_y"); I(iv_x, "@x"); I(iv_y, "@y");
    I(iv_jump_count, "@jump_count"); I(iv_jump_peak, "@jump_peak"); I(iv_move_speed, "@move_speed");
    I(iv_tile_id, "@tile_id"); I(iv_character_name, "@character_name"); I(iv_priority_type, "@priority_type");
    I(iv_locked, "@locked"); I(iv_stop_count, "@stop_count"); I(iv_anime_count, "@anime_count");
    I(iv_walk_anime, "@walk_anime"); I(iv_step_anime, "@step_anime"); I(iv_pattern, "@pattern");
    I(iv_original_pattern, "@original_pattern"); I(iv_move_route_forcing, "@move_route_forcing");
    I(iv_move_type, "@move_type"); I(iv_move_frequency, "@move_frequency"); I(iv_trigger, "@trigger");
    I(iv_interpreter, "@interpreter"); I(iv_list, "@list"); I(iv_event, "@event");
    I(iv_display_x, "@display_x"); I(iv_display_y, "@display_y"); I(iv_map, "@map"); I(iv_events, "@events");
    I(iv_common_events, "@common_events"); I(iv_animation, "@animation"); I(iv_character, "@character");
    I(iv_character_index, "@character_index"); I(iv_cw, "@cw"); I(iv_ch, "@ch");
    I(iv_balloon_duration, "@balloon_duration"); I(iv_balloon_sprite, "@balloon_sprite");
    I(iv_balloon_id, "@balloon_id"); I(iv_character_sprites, "@character_sprites"); I(iv_map_id, "@map_id");
    I(cv_ani_checker, "@@ani_checker"); I(cv_ani_spr_checker, "@@ani_spr_checker");
    I(gv_game_map, "$game_map"); I(gv_data_animations, "$data_animations");
    I(iv_symbol_encount, "@symbol_encount"); I(iv_wait_count, "@wait_count");
    I(iv_visibility_distance, "@visibility_distance"); I(iv_origin_opacity, "@origin_opacity");
    I(iv_opacity, "@opacity"); I(iv_uninhibited_movement, "@uninhibited_movement");
    I(iv_region_z_pos, "@region_z_pos"); I(iv_first_z_pos, "@first_z_pos"); I(iv_anti_lag, "@anti_lag");
    I(iv_can_update, "@can_update"); I(iv_data, "@data");
    I(id_update_symbol_reaction, "update_symbol_reaction"); I(id_update_symbol_movement, "update_symbol_movement");
    I(id_zero, "zero?"); I(id_truncate, "truncate"); I(id_between, "between?");
    I(id_reset_sprite_effects, "reset_sprite_effects"); I(id_is_a, "is_a?"); I(id_MOVE_CONTROL, "MOVE_CONTROL");
    I(id_DASH_PLUS, "DASH_PLUS"); I(id_MOG_ANTI_LAG, "MOG_ANTI_LAG");
    I(id_UPDATE_OUT_SCREEN_RANGE, "UPDATE_OUT_SCREEN_RANGE"); I(id_Game_Event, "Game_Event");
    I(gv_game_variables, "$game_variables"); I(gv_game_system, "$game_system");
#undef I

    slotDef(S_GCB_moving, "Game_CharacterBase", "moving?");
    slotDef(S_GCB_jumping, "Game_CharacterBase", "jumping?");
    slotDef(S_GCB_stopping, "Game_CharacterBase", "stopping?");
    slotDef(S_GCB_real_move_speed, "Game_CharacterBase", "real_move_speed");
    slotDef(S_GCB_distance_per_frame, "Game_CharacterBase", "distance_per_frame");
    slotDef(S_GCB_dash, "Game_CharacterBase", "dash?");
    slotDef(S_GCB_object_character, "Game_CharacterBase", "object_character?");
    slotDef(S_GCB_shift_y, "Game_CharacterBase", "shift_y");
    slotDef(S_GCB_jump_height, "Game_CharacterBase", "jump_height");
    slotDef(S_GCB_screen_x, "Game_CharacterBase", "screen_x");
    slotDef(S_GCB_screen_y, "Game_CharacterBase", "screen_y");
    slotDef(S_GCB_screen_z, "Game_CharacterBase", "screen_z");
    slotDef(S_GCB_normal_priority, "Game_CharacterBase", "normal_priority?");
    slotDef(S_GCB_update, "Game_CharacterBase", "update");
    slotDef(S_GCB_update_move, "Game_CharacterBase", "update_move");
    slotDef(S_GCB_update_stop, "Game_CharacterBase", "update_stop");
    slotDef(S_GCB_update_animation, "Game_CharacterBase", "update_animation");
    slotDef(S_GCB_update_anime_count, "Game_CharacterBase", "update_anime_count");
    slotDef(S_GCB_update_anime_pattern, "Game_CharacterBase", "update_anime_pattern");
    slotDef(S_GCB_update_jump, "Game_CharacterBase", "update_jump");
    slotDef(S_GCB_update_bush_depth, "Game_CharacterBase", "update_bush_depth");
    slotDef(S_GCB_tile_id, "Game_CharacterBase", "tile_id");
    slotDef(S_GCB_character_name, "Game_CharacterBase", "character_name");
    slotDef(S_GCB_character_index, "Game_CharacterBase", "character_index");
    slotDef(S_GCB_pattern, "Game_CharacterBase", "pattern");
    slotDef(S_GCB_direction, "Game_CharacterBase", "direction");
    slotDef(S_GCB_opacity, "Game_CharacterBase", "opacity");
    slotDef(S_GCB_blend_type, "Game_CharacterBase", "blend_type");
    slotDef(S_GCB_bush_depth, "Game_CharacterBase", "bush_depth");
    slotDef(S_GCB_transparent, "Game_CharacterBase", "transparent");
    slotDef(S_GCB_animation_id, "Game_CharacterBase", "animation_id");
    slotDef(S_GCB_balloon_id, "Game_CharacterBase", "balloon_id");
    slotDef(S_GC_update_stop, "Game_Character", "update_stop");
    slotDef(S_GC_update_routine_move, "Game_Character", "update_routine_move");
    slotDef(S_GE_update, "Game_Event", "update");
    slotDef(S_GE_update_stop, "Game_Event", "update_stop");
    slotDef(S_GE_update_self_movement, "Game_Event", "update_self_movement");
    slotDef(S_GE_near_the_screen, "Game_Event", "near_the_screen?");
    slotDef(S_GE_stop_count_threshold, "Game_Event", "stop_count_threshold");
    slotDef(S_GE_check_event_trigger_auto, "Game_Event", "check_event_trigger_auto");
    slotDef(S_GE_move_type_random, "Game_Event", "move_type_random");
    slotDef(S_GE_move_type_toward_player, "Game_Event", "move_type_toward_player");
    slotDef(S_GE_move_type_custom, "Game_Event", "move_type_custom");
    slotDef(S_GE_start, "Game_Event", "start");
    slotDef(S_GM_adjust_x, "Game_Map", "adjust_x");
    slotDef(S_GM_adjust_y, "Game_Map", "adjust_y");
    slotDef(S_GM_loop_horizontal, "Game_Map", "loop_horizontal?");
    slotDef(S_GM_loop_vertical, "Game_Map", "loop_vertical?");
    slotDef(S_GM_width, "Game_Map", "width");
    slotDef(S_GM_height, "Game_Map", "height");
    slotDef(S_GM_screen_tile_x, "Game_Map", "screen_tile_x");
    slotDef(S_GM_screen_tile_y, "Game_Map", "screen_tile_y");
    slotDef(S_GM_update_events, "Game_Map", "update_events");
    slotDef(S_MAP_scroll_type, "RPG::Map", "scroll_type");
    slotDef(S_MAP_width, "RPG::Map", "width");
    slotDef(S_MAP_height, "RPG::Map", "height");
    slotDef(S_EVENT_id, "RPG::Event", "id");
    slotDef(S_SB_update, "Sprite_Base", "update");
    slotDef(S_SB_animation, "Sprite_Base", "animation?");
    slotDef(S_SB_update_animation, "Sprite_Base", "update_animation");
    slotDef(S_SC_update, "Sprite_Character", "update");
    slotDef(S_SC_update_bitmap, "Sprite_Character", "update_bitmap");
    slotDef(S_SC_graphic_changed, "Sprite_Character", "graphic_changed?");
    slotDef(S_SC_update_src_rect, "Sprite_Character", "update_src_rect");
    slotDef(S_SC_update_position, "Sprite_Character", "update_position");
    slotDef(S_SC_update_other, "Sprite_Character", "update_other");
    slotDef(S_SC_update_balloon, "Sprite_Character", "update_balloon");
    slotDef(S_SC_setup_new_effect, "Sprite_Character", "setup_new_effect");
    slotDef(S_SC_move_animation, "Sprite_Character", "move_animation");
    slotDef(S_SC_set_tile_bitmap, "Sprite_Character", "set_tile_bitmap");
    slotDef(S_SC_set_character_bitmap, "Sprite_Character", "set_character_bitmap");
    slotDef(S_SC_start_animation, "Sprite_Character", "start_animation");
    slotDef(S_SC_start_balloon, "Sprite_Character", "start_balloon");
    slotDef(S_SC_sp1_orig, "alias", "vxace_sp1_update_position");
    slotDef(S_GC_update, "Game_Character", "update");
    slotDef(S_GC_update_symbol_opacity, "Game_Character", "update_symbol_opacity");
    slotDef(S_GE_update_symbol_opacity, "Game_Event", "update_symbol_opacity");
    slotDef(S_GE_self_movable, "Game_Event", "self_movable?");
    slotDef(S_GE_update_anti_lag, "Game_Event", "update_anti_lag");
    slotDef(S_GE_anti_lag_on_screen, "Game_Event", "anti_lag_event_on_screen");
    slotDef(S_GE_can_update, "Game_Event", "can_update");
    slotDef(S_GV_aref, "Game_Variables", "[]");
    slotDef(S_GS_anti_lag, "Game_System", "anti_lag");
    slotDef(S_GM_display_x, "Game_Map", "display_x");
    slotDef(S_GM_display_y, "Game_Map", "display_y");
    slotDef(S_SC_check_can_update_sprite, "Sprite_Character", "check_can_update_sprite");
    slotDef(S_SPR_visible, "Sprite", "visible");
    /* alias names the wrappers call; "alias" is no class name, so replace/attr never pick them */
    slotDef(S_H_GE_update_symbol, "alias", "_update_with_symbol");
    slotDef(S_H_GE_update_mog, "alias", "mog_anti_lag_update");
    slotDef(S_H_GC_update_symbol, "alias", "_symbol_opacity_update");
    slotDef(S_H_GE_usm_symbol, "alias", "_update_self_movement_with_symbol");
    slotDef(S_H_GCB_rms_8dir, "alias", "real_move_speed_8direction");
    slotDef(S_H_SC_update_mog, "alias", "mog_anti_lag_update");
    slotDef(S_ARY_aref, "Array", "[]");
    slotDef(S_INT_zero, "Integer", "zero?");
    slotDef(S_INT_truncate, "Integer", "truncate");
    slotDef(S_FLO_truncate, "Float", "truncate");
    slotDef(S_INT_between, "Integer", "between?");
    slotDef(S_OBJ_is_a, "Object", "is_a?");
    slotDef(S_SM_update_characters, "Spriteset_Map", "update_characters");
    slotDef(S_SM_refresh_characters, "Spriteset_Map", "refresh_characters");
    slotDef(S_SPR_update, "Sprite", "update");
    slotDef(S_SPR_x, "Sprite", "x");
    slotDef(S_SPR_y, "Sprite", "y");
    slotDef(S_SPR_set_x, "Sprite", "x=");
    slotDef(S_SPR_set_y, "Sprite", "y=");
    slotDef(S_SPR_set_z, "Sprite", "z=");
    slotDef(S_SPR_set_opacity, "Sprite", "opacity=");
    slotDef(S_SPR_set_blend_type, "Sprite", "blend_type=");
    slotDef(S_SPR_set_bush_depth, "Sprite", "bush_depth=");
    slotDef(S_SPR_set_visible, "Sprite", "visible=");
    slotDef(S_SPR_src_rect, "Sprite", "src_rect");
    slotDef(S_RECT_set, "Rect", "set");
    slotDef(S_GFX_width, "Graphics", "width");
    slotDef(S_GFX_height, "Graphics", "height");
    slotDef(S_ARY_clear, "Array", "clear");
    slotDef(S_STR_eq, "String", "==");
    slotDef(S_STR_neq, "String", "!=");

    int state = 0;
    rb_protect(initRuby, Qnil, &state);
    if (state) {
        /* Native events stay off; the game runs its Ruby methods. */
        VALUE err = rb_errinfo();
        rb_set_errinfo(Qnil);
        VALUE msg = rb_protect([](VALUE e) { return rb_funcall(e, rb_intern("inspect"), 0); }, err, &state);
        fprintf(stderr, "native events off: %s\n", state ? "?" : StringValueCStr(msg));
        rb_set_errinfo(Qnil);
        g_module = Qnil;
    }
}


/* HardRPGNative.cbench(sprite, character, n): C-level cost of the calls the native code makes
 * (microseconds per call). Measurement only. */
static double nowUs() {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static VALUE nev_cbench(VALUE, VALUE spr, VALUE chr, VALUE nv) {
    int n = NUM2INT(nv);
    VALUE h = rb_hash_new();
    auto rec = [&](const char *k, double t0) { rb_hash_aset(h, rb_str_new_cstr(k), DBL2NUM((nowUs() - t0) / n)); };
    VALUE five = INT2FIX(5);
    double t0 = nowUs();
    for (int i = 0; i < n; ++i) { VALUE v = five; (void)v; }
    rec("empty", t0);
    Slot &sx = g_slots[S_SPR_set_x];
    VALUE cur = rb_funcall(spr, rb_intern("x"), 0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) sx.engine(1, &cur, spr);
    rec("engine x= direct cfunc", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) callSlot(spr, S_SPR_set_x, 1, &cur);
    rec("callSlot x= (check+cfunc)", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) rb_funcallv(spr, sx.mid, 1, &cur);
    rec("rb_funcall x=", t0);
    Sprite *sp = getPrivateData<Sprite>(spr);
    int cx = sp->getX();
    t0 = nowUs();
    for (int i = 0; i < n; ++i) sp->setX(cx);
    rec("C++ Sprite::setX", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) getPrivateData<Sprite>(spr)->setX(cx);
    rec("getPrivateData+setX", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) { shState->graphics().lock(); shState->graphics().unlock(); }
    rec("Graphics lock+unlock", t0);
    {
        SDL_mutex *m = SDL_CreateMutex();
        t0 = nowUs();
        for (int i = 0; i < n; ++i) { SDL_LockMutex(m); SDL_UnlockMutex(m); }
        rec("SDL mutex lock+unlock", t0);
        SDL_DestroyMutex(m);
        SDL_Window *w = SDL_GL_GetCurrentWindow();
        SDL_GLContext c = SDL_GL_GetCurrentContext();
        t0 = nowUs();
        for (int i = 0; i < n; ++i) SDL_GL_MakeCurrent(w, c);
        rec("SDL_GL_MakeCurrent (same ctx)", t0);
        t0 = nowUs();
        for (int i = 0; i < n; ++i) (void)SDL_GL_GetCurrentContext();
        rec("SDL_GL_GetCurrentContext", t0);
        struct TagProbe { static int run(void *out) {
            void *tp; __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tp));
            *(void **)out = tp; return 0; } };
        void *mainTag, *otherTag = nullptr;
        __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(mainTag));
        SDL_Thread *th = SDL_CreateThread(TagProbe::run, "tagprobe", &otherTag);
        int rc = 0;
        SDL_WaitThread(th, &rc);
        char buf[96];
        snprintf(buf, sizeof buf, "main=%p other=%p %s", mainTag, otherTag, mainTag != otherTag && otherTag ? "distinct" : "SAME");
        rb_hash_aset(h, rb_str_new_cstr("thread tags"), rb_str_new_cstr(buf));
        rb_hash_aset(h, rb_str_new_cstr("threadsafe"), shState->graphics().getThreadsafe() ? Qtrue : Qfalse);
    }
    ID iv = rb_intern("@real_x");
    t0 = nowUs();
    for (int i = 0; i < n; ++i) rb_ivar_get(chr, iv);
    rec("rb_ivar_get", t0);
    {
        static IvSite site;
        t0 = nowUs();
        for (int i = 0; i < n; ++i) ivFast(chr, iv, site);
        rec("ivFast (cached)", t0);
    }
    if (!NIL_P(g_spriteBase)) {
        t0 = nowUs();
        for (int i = 0; i < n; ++i) rb_cvar_get(g_spriteBase, cv_ani_checker);
        rec("rb_cvar_get @@ani_checker", t0);
    }
    t0 = nowUs();
    for (int i = 0; i < n; ++i) gameMap();
    rec("gameMap() gvar", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) DBL2NUM(1.5 + i);
    rec("DBL2NUM alloc", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) slotOk(g_slots[S_GCB_screen_x], rb_class_of(chr));
    rec("slotOk hit", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) callSlot(chr, S_GCB_screen_x, 0, nullptr);
    rec("native screen_x", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) callSlot(chr, S_GCB_screen_y, 0, nullptr);
    rec("native screen_y", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) rb_funcallv(chr, rb_intern("screen_y"), 0, nullptr);
    rec("rb_funcall screen_y", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) rb_funcallv(chr, rb_intern("object_id"), 0, nullptr);
    rec("rb_funcall object_id (cfunc)", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) callSlot(spr, S_SPR_src_rect, 0, nullptr);
    rec("callSlot src_rect", t0);
    t0 = nowUs();
    for (int i = 0; i < n; ++i) callSlot(spr, S_SC_update, 0, nullptr);
    rec("native Sprite_Character#update", t0);
    for (int id : {S_SC_update_bitmap, S_SC_graphic_changed, S_SC_update_src_rect, S_SC_update_position,
                   S_SC_update_other, S_SC_update_balloon, S_SC_setup_new_effect, S_SB_update,
                   S_SB_update_animation, S_SPR_update, S_GCB_screen_z, S_GCB_shift_y, S_GCB_jump_height,
                   S_GE_update, S_GCB_update, S_GCB_update_animation, S_GCB_update_stop, S_GE_update_stop,
                   S_GE_check_event_trigger_auto, S_GCB_moving}) {
        bool ev = g_slots[id].owner_name[0] == 'G';
        if (ev && id == S_GE_update)
            continue; /* runs the interpreter; not repeatable here */
        t0 = nowUs();
        for (int i = 0; i < n; ++i) callSlot(ev ? chr : spr, id, 0, nullptr);
        std::string k = std::string(g_slots[id].owner_name) + "#" + g_slots[id].name;
        rec(k.c_str(), t0);
    }
    return h;
}

static VALUE initRuby(VALUE) {
    /* The module and `bump` exist before the change hooks are installed: the hooks call bump. */
    g_module = rb_define_module("HardRPGNative");
    rb_define_singleton_method(g_module, "bump", RUBY_METHOD_FUNC(nev_bump), 0);
    rb_eval_string(kHelpers);
    rb_define_singleton_method(g_module, "replace", RUBY_METHOD_FUNC(nev_replace), 3);
    rb_define_singleton_method(g_module, "attr", RUBY_METHOD_FUNC(nev_attr), 2);
    rb_define_singleton_method(g_module, "stats", RUBY_METHOD_FUNC(nev_stats), 0);
    rb_define_singleton_method(g_module, "reset_stats", RUBY_METHOD_FUNC(nev_reset_stats), 0);
    rb_define_singleton_method(g_module, "epoch", RUBY_METHOD_FUNC(nev_epoch), 0);
    rb_define_singleton_method(g_module, "checks", RUBY_METHOD_FUNC(nev_checks), 0);
    rb_define_singleton_method(g_module, "cbench", RUBY_METHOD_FUNC(nev_cbench), 3);

    /* Engine methods the native code calls. */
    VALUE sprite = rb_path2class("Sprite");
    for (int id : {S_SPR_update, S_SPR_x, S_SPR_y, S_SPR_set_x, S_SPR_set_y, S_SPR_set_z,
                   S_SPR_set_opacity, S_SPR_set_blend_type, S_SPR_set_bush_depth, S_SPR_set_visible,
                   S_SPR_src_rect, S_SPR_visible})
        engineSlot(id, sprite);
    engineSlot(S_RECT_set, rb_path2class("Rect"));
    g_graphics = rb_path2class("Graphics");
    engineSlot(S_GFX_width, rb_singleton_class(g_graphics));
    engineSlot(S_GFX_height, rb_singleton_class(g_graphics));
    g_slots[S_GFX_width].owner_name = g_slots[S_GFX_height].owner_name = "Graphics.";
    /* Builtins the native code does itself (Array#clear, String#==, String#!=): only while
     * they are not redefined, which the resolution check sees. */
    struct { int id; VALUE klass; } builtins[] = {
        {S_ARY_clear, rb_cArray}, {S_STR_eq, rb_cString}, {S_STR_neq, rb_cString}, {S_ARY_aref, rb_cArray},
        {S_INT_zero, rb_cInteger}, {S_INT_truncate, rb_cInteger}, {S_FLO_truncate, rb_cFloat},
        {S_INT_between, rb_cInteger}, {S_OBJ_is_a, rb_cObject}};
    for (auto &b : builtins) {
        Slot &s = g_slots[b.id];
        /* The method that class has now (String#!= is BasicObject#!=, which calls ==;
         * Integer#between? is Comparable's; is_a? is Kernel's). */
        VALUE um = instanceMethod(b.klass, s.mid);
        s.kind = SK_ENGINE; /* never called through .engine; only checked */
        s.engine = nullptr;
        s.owner = rb_funcall(um, rb_intern("owner"), 0);
        s.expect = instanceMethod(s.owner, s.mid); /* what resolves_to? compares with */
        rb_gc_register_mark_object(s.expect);
    }
    /* RPG data classes come from the engine's module_rpg3 (attr_accessors). */
    attrSlotFrom(S_MAP_scroll_type, rpgClass("RPG::Map"));
    attrSlotFrom(S_MAP_width, rpgClass("RPG::Map"));
    attrSlotFrom(S_MAP_height, rpgClass("RPG::Map"));
    attrSlotFrom(S_EVENT_id, rpgClass("RPG::Event"));
    id_src_rect_iv = rb_intern("src_rect");
    g_float_256 = DBL2NUM(256.0);
    rb_gc_register_mark_object(g_float_256);
    return Qnil;
}

/* After each game script section ran: let the policy script swap defaults. */
static VALUE sectionHook(VALUE arg) {
    VALUE *a = (VALUE *)arg;
    return rb_funcall(g_module, rb_intern("section_loaded"), 2, a[0], a[1]);
}

void nativeEventsSectionLoaded(VALUE fname, VALUE source) {
    if (NIL_P(g_module) || !rb_respond_to(g_module, rb_intern("section_loaded")))
        return;
    if (g_spriteBase == Qnil && rb_const_defined(rb_cObject, rb_intern("Sprite_Base")))
        g_spriteBase = rb_const_get(rb_cObject, rb_intern("Sprite_Base"));
    VALUE a[2] = {fname, source};
    int state = 0;
    rb_protect(sectionHook, (VALUE)a, &state);
    if (state)
        rb_set_errinfo(Qnil); /* the policy script failing must not stop the game */
}
