/*
** essentials-natives.cpp
**
** Native versions of Pokemon Essentials (RPG Maker XP) methods that run many times a frame:
**  - Game_Map#terrain_tag, #bush?, #deepBush? (every character sprite and reflection, every frame)
**  - Game_Character#bush_depth
**  - Game_Map#in_range? and Spriteset_Map#in_range? (which events and sprites update)
**  - ReflectedSprite#update (one per event: is the event above still water?)
**  - The SpriteResizer Sprite#x, #y, #ox, #oy, #zoom_x, #zoom_y readers and writers, which
**    Essentials puts on every sprite
**
** launcher/essentials_natives.rb decides when to use them: only where the game's method is one of
** the known texts (the variants in g_natives), and only while what they lean on is the default.
** A native does exactly what its text does, calling the same methods. When a value is not the
** plain kind the fast path handles (a Float where an Integer is usual, a resize factor other than
** 100, a reflection that has to be drawn), it runs the game's own Ruby method instead.
*/

#include "binding-util.h"
#include "table.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

DECL_TYPE(Table);

namespace {

typedef VALUE (*NativeFn)(int, VALUE *, VALUE);

ID iv_map_id, iv_terrain_tags, iv_passages, id_data, id_bridge, id_aref;
ID id_Bridge, id_Shadow, id_TallGrass, id_StillWater, id_TILEHEIGHT;
ID iv_tile_id, iv_always_on_top, iv_jump_count, iv_direction, iv_x, iv_y;
ID id_map, id_bush, id_deepBush, id_moving, id_terrain_tag, id_display_x, id_tilemap;
ID id_real_x, id_real_y, id_width, id_height, id_x, id_visible, id_disposed, id_dispose;
ID id_src_rect, id_to_i, id_viewport, id_plus, id_display_y, id_y, id_xeq_plain;
ID iv_animations, iv_loopAnimations, id_MAPVIEWMODE, iv_character, iv_character_name, iv_character_hue;
ID iv_oldbushdepth, iv_charbitmapAnimated, iv_charbitmap, iv_bushbitmap, iv_cw, iv_ch, iv_spriteoffset;
ID id_tile_id, id_character_name, id_character_hue, id_bush_depth, id_sc_changed, id_update, id_bitmap;
ID id_bitmap_set, id_visible_set, id_transparent, id_pattern, id_direction, id_set, id_oy_set, id_oy;
ID id_bob_height, id_name, id_tone, id_pbDayNightTint, id_x_set, id_y_set, id_z_set, id_screen_x;
ID id_screen_y, id_screen_z, id_zoom_x_set, id_zoom_y_set, id_opacity_set, id_opacity, id_blend_type_set;
ID id_blend_type, id_animation_id;
VALUE g_regularTone = Qnil;
ID iv_map, iv_event, iv_rsprite, iv_sprite;
ID iv_resizedX, iv_resizedY, iv_resizedOx, iv_resizedOy, iv_resizedZoomX, iv_resizedZoomY;
ID id_xeq, id_yeq, id_oxeq, id_oyeq, id_zoomxeq, id_zoomyeq;
VALUE g_graphics = Qnil;

/* PBTerrain and map constants, read on first use (PBTerrain is defined after Game_Map). */
struct Tags {
    bool ready = false;
    long bridge = -1, shadow = -1, tallGrass = -1, stillWater = -1, tileHeight = 32;
} g_tags;

long constOr(VALUE mod, ID id, long fallback) {
    if (!rb_const_defined_at(mod, id))
        return fallback;
    VALUE v = rb_const_get_at(mod, id);
    return FIXNUM_P(v) ? FIX2LONG(v) : fallback;
}

bool loadTags() {
    if (g_tags.ready)
        return true;
    if (!rb_const_defined(rb_cObject, rb_intern("PBTerrain")) ||
        !rb_const_defined(rb_cObject, rb_intern("Game_Map")))
        return false;
    VALUE t = rb_const_get(rb_cObject, rb_intern("PBTerrain"));
    g_tags.bridge = constOr(t, id_Bridge, -1);
    g_tags.shadow = constOr(t, id_Shadow, -1);
    g_tags.tallGrass = constOr(t, id_TallGrass, -1);
    g_tags.stillWater = constOr(t, id_StillWater, -1);
    VALUE gm = rb_const_get(rb_cObject, rb_intern("Game_Map"));
    if (!rb_const_defined(gm, id_TILEHEIGHT))
        return false;
    VALUE th = rb_const_get(gm, id_TILEHEIGHT);
    if (!FIXNUM_P(th) || FIX2LONG(th) <= 0)
        return false;
    g_tags.tileHeight = FIX2LONG(th);
    g_tags.ready = true;
    return true;
}

inline VALUE call0(VALUE o, ID m) { return rb_funcallv(o, m, 0, nullptr); }

/* table[i] / table[x, y, z] as Table#[] does it: nil outside, else the signed short.
 * Not a Table (some script replaced it): ask Ruby. */
inline VALUE tableAt(VALUE table, VALUE x, VALUE y, VALUE z, int argc) {
    if (rb_typeddata_is_kind_of(table, &TableType) && FIXNUM_P(x) && (argc < 2 || FIXNUM_P(y)) &&
        (argc < 3 || FIXNUM_P(z))) {
        Table *t = static_cast<Table *>(RTYPEDDATA_DATA(table));
        long ix = FIX2LONG(x), iy = argc > 1 ? FIX2LONG(y) : 0, iz = argc > 2 ? FIX2LONG(z) : 0;
        if (!t || ix < 0 || ix >= t->xSize() || iy < 0 || iy >= t->ySize() || iz < 0 || iz >= t->zSize())
            return Qnil;
        return INT2FIX(t->get((int)ix, (int)iy, (int)iz));
    }
    VALUE args[3] = {x, y, z};
    return rb_funcallv(table, id_aref, argc, args);
}

inline VALUE tagOf(VALUE tags, VALUE tile) { return tableAt(tags, tile, Qnil, Qnil, 1); }

/* `x == C` for a tag read from a Table (Integer or nil); C missing (-1) never matches. */
inline bool tagIs(VALUE tag, long c) { return c >= 0 && FIXNUM_P(tag) && FIX2LONG(tag) == c; }

/* $PokemonMap && $PokemonMap.bridge: the count (truthy false without $PokemonMap). */
inline long bridgeCount(bool &truthy) {
    VALUE pm = rb_gv_get("$PokemonMap");
    truthy = RTEST(pm);
    if (!truthy)
        return 0;
    VALUE b = call0(pm, id_bridge);
    if (FIXNUM_P(b))
        return FIX2LONG(b);
    return RTEST(rb_funcall(b, rb_intern(">"), 1, INT2FIX(0))) ? 1
           : RTEST(rb_funcall(b, rb_intern("=="), 1, INT2FIX(0))) ? 0 : -1;
}

bool mapIdZero(VALUE self) {
    VALUE id = rb_ivar_get(self, iv_map_id);
    return FIXNUM_P(id) && FIX2LONG(id) == 0;
}

/* ------------------------------------------------------------------ */
/* The natives. Each returns Qundef when only the Ruby method can answer exactly. */

enum { V_URANIUM = 0, V_CLASSIC = 1 };

/* Game_Map#terrain_tag(x, y, countBridge=false) */
VALUE terrainTag(int argc, VALUE *argv, VALUE self, int variant) {
    if (argc < 2 || argc > 3)
        return Qundef;
    if (mapIdZero(self))
        return INT2FIX(0);
    if (!loadTags())
        return Qundef;
    VALUE x = argv[0], y = argv[1];
    bool countBridge = argc > 2 && RTEST(argv[2]);
    VALUE data = call0(self, id_data);
    VALUE tags = rb_ivar_get(self, iv_terrain_tags);
    for (int i = 2; i >= 0; --i) {
        VALUE tile = tableAt(data, x, y, INT2FIX(i), 3);
        if (NIL_P(tile))
            return INT2FIX(0);
        VALUE tag = tagOf(tags, tile);
        if (tagIs(tag, g_tags.bridge) && !countBridge) {
            bool pm;
            long b = bridgeCount(pm);
            if (pm && b == 0)
                continue;
        }
        if (variant == V_URANIUM && tagIs(tag, g_tags.shadow))
            continue;
        if (FIXNUM_P(tag) && FIX2LONG(tag) > 0)
            return tag;
        if (RTEST(tag) && !FIXNUM_P(tag))
            return Qundef;
    }
    return INT2FIX(0);
}

/* Game_Map#bush?(x, y) and #deepBush?(x, y) */
VALUE bushCheck(VALUE self, VALUE x, VALUE y, bool deep) {
    if (mapIdZero(self))
        return Qfalse;
    if (!loadTags())
        return Qundef;
    VALUE data = call0(self, id_data);
    VALUE tags = rb_ivar_get(self, iv_terrain_tags);
    VALUE passages = rb_ivar_get(self, iv_passages);
    for (int i = 2; i >= 0; --i) {
        VALUE tile = tableAt(data, x, y, INT2FIX(i), 3);
        if (NIL_P(tile))
            return Qfalse;
        VALUE tag = tagOf(tags, tile);
        if (tagIs(tag, g_tags.bridge)) {
            bool pm;
            long b = bridgeCount(pm);
            if (pm && b > 0)
                return Qfalse;
        }
        VALUE pass = tagOf(passages, tile);
        if (!FIXNUM_P(pass))
            return Qundef; /* nil & 0x40 raises in Ruby: let Ruby raise it */
        if ((FIX2LONG(pass) & 0x40) == 0x40 && (!deep || tagIs(tag, g_tags.tallGrass)))
            return Qtrue;
    }
    return Qfalse;
}

VALUE nTerrainU(int argc, VALUE *argv, VALUE self) { return terrainTag(argc, argv, self, V_URANIUM); }
VALUE nTerrainC(int argc, VALUE *argv, VALUE self) { return terrainTag(argc, argv, self, V_CLASSIC); }
VALUE nBush(int argc, VALUE *argv, VALUE self) {
    return argc == 2 ? bushCheck(self, argv[0], argv[1], false) : Qundef;
}
VALUE nDeepBush(int argc, VALUE *argv, VALUE self) {
    return argc == 2 ? bushCheck(self, argv[0], argv[1], true) : Qundef;
}

/* Game_Character#bush_depth. Uranium:
 *   return 0 if @tile_id>0 or @always_on_top
 *   if @jump_count <= 0
 *     xbehind/ybehind from @direction
 *     return 32 if map.deepBush?(@x,@y) and map.deepBush?(xbehind,ybehind)
 *     return 12 if map.bush?(@x,@y) and !moving?
 *   end
 *   return 0
 * Classic: return 0 if @tile_id > 0 or @always_on_top;
 *   (@jump_count <= 0 and map.bush?(@x, @y)) ? 12 : 0 */
VALUE bushDepth(VALUE self, int variant) {
    VALUE tile = rb_ivar_get(self, iv_tile_id), jump = rb_ivar_get(self, iv_jump_count);
    if (!FIXNUM_P(tile) || !FIXNUM_P(jump))
        return Qundef;
    if (FIX2LONG(tile) > 0 || RTEST(rb_ivar_get(self, iv_always_on_top)))
        return INT2FIX(0);
    if (FIX2LONG(jump) > 0)
        return INT2FIX(0);
    VALUE x = rb_ivar_get(self, iv_x), y = rb_ivar_get(self, iv_y);
    VALUE map = call0(self, id_map);
    VALUE xy[2] = {x, y};
    if (variant == V_URANIUM) {
        VALUE dir = rb_ivar_get(self, iv_direction);
        if (!FIXNUM_P(dir) || !FIXNUM_P(x) || !FIXNUM_P(y))
            return Qundef;
        long d = FIX2LONG(dir), bx = FIX2LONG(x), by = FIX2LONG(y);
        bx = d == 4 ? bx + 1 : d == 6 ? bx - 1 : bx;
        by = d == 8 ? by + 1 : d == 2 ? by - 1 : by;
        if (RTEST(rb_funcallv(map, id_deepBush, 2, xy))) {
            VALUE behind[2] = {LONG2FIX(bx), LONG2FIX(by)};
            if (RTEST(rb_funcallv(map, id_deepBush, 2, behind)))
                return INT2FIX(32);
        }
        if (RTEST(rb_funcallv(map, id_bush, 2, xy)) && !RTEST(call0(self, id_moving)))
            return INT2FIX(12);
        return INT2FIX(0);
    }
    return RTEST(rb_funcallv(map, id_bush, 2, xy)) ? INT2FIX(12) : INT2FIX(0);
}

VALUE nBushDepthU(int argc, VALUE *, VALUE self) { return argc == 0 ? bushDepth(self, V_URANIUM) : Qundef; }
VALUE nBushDepthC(int argc, VALUE *, VALUE self) { return argc == 0 ? bushDepth(self, V_CLASSIC) : Qundef; }

/* in_range?(object): `$PokemonSystem.tilemap==2` answers true; otherwise the object's
 * real_x/real_y must be strictly inside the display area grown by `border` (in real units).
 * Spriteset_Map reads @map.display_x, Game_Map its own display_x. Essentials keeps display and
 * real positions as Floats or Integers: done in doubles, which hold both exactly, adding in the
 * text's order (Uranium: display_x + Graphics.width*4 + border; classic: display_x plus
 * (Graphics.width*4+border)). */
inline bool numOf(VALUE v, double &d) {
    if (FIXNUM_P(v)) { d = (double)FIX2LONG(v); return true; }
    if (RB_FLOAT_TYPE_P(v)) { d = RFLOAT_VALUE(v); return true; }
    return false;
}

VALUE inRange(VALUE self, VALUE obj, VALUE map, long border, bool grouped) {
    VALUE ps = rb_gv_get("$PokemonSystem");
    VALUE tm = call0(ps, id_tilemap);
    if (FIXNUM_P(tm) && FIX2LONG(tm) == 2)
        return Qtrue;
    if (!FIXNUM_P(tm))
        return Qundef;
    double dx, dy, rx, ry;
    VALUE gw = call0(g_graphics, id_width), gh = call0(g_graphics, id_height);
    if (!numOf(call0(map, id_display_x), dx) || !numOf(call0(map, id_display_y), dy) ||
        !FIXNUM_P(gw) || !FIXNUM_P(gh) || !numOf(call0(obj, id_real_x), rx) ||
        !numOf(call0(obj, id_real_y), ry))
        return Qundef;
    double w4 = (double)(FIX2LONG(gw) * 4), h4 = (double)(FIX2LONG(gh) * 4), b = (double)border;
    double x0 = dx - b, y0 = dy - b;
    double x1 = grouped ? dx + (w4 + b) : (dx + w4) + b;
    double y1 = grouped ? dy + (h4 + b) : (dy + h4) + b;
    if (rx <= x0 || rx >= x1 || ry <= y0 || ry >= y1)
        return Qfalse;
    return Qtrue;
}

VALUE nSpritesetInRangeU(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? inRange(self, argv[0], rb_ivar_get(self, iv_map), 192 * 4, false) : Qundef;
}
VALUE nSpritesetInRangeC(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? inRange(self, argv[0], rb_ivar_get(self, iv_map), 512, true) : Qundef;
}
VALUE nMapInRangeU(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? inRange(self, argv[0], self, 192 * 4, false) : Qundef;
}
VALUE nMapInRangeC(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? inRange(self, argv[0], self, 256, true) : Qundef;
}

/* ReflectedSprite#update. The clipping loop marks the reflection shown only when the tile right
 * below the event (currentY+1) is still water; any other tile breaks out at once. So unless that
 * one tile is still water (and the reflection is visible) the method just drops its sprite:
 *   if @sprite; @sprite.dispose; @sprite=nil; end
 * which is all done here. A reflection that is drawn runs the Ruby method. The classic text has
 * `shouldShow=true` commented out: its reflections are never drawn. */
VALUE reflectedUpdate(VALUE self, int variant) {
    if (RTEST(call0(self, id_disposed)))
        return Qnil;
    if (variant == V_URANIUM) {
        if (!loadTags() || g_tags.stillWater < 0)
            return Qundef;
        VALUE ev = rb_ivar_get(self, iv_event), rs = rb_ivar_get(self, iv_rsprite);
        VALUE ry = call0(ev, id_real_y);
        if (!FIXNUM_P(ry))
            ry = call0(ry, id_to_i);
        if (!FIXNUM_P(ry))
            return Qundef;
        VALUE h = call0(call0(rs, id_src_rect), id_height);
        if (!FIXNUM_P(h))
            return Qundef;
        long th = g_tags.tileHeight;
        if (FIX2LONG(h) + th > 0) {
            long v = FIX2LONG(ry), q = 4 * th;
            long cur = v / q - ((v % q != 0) && ((v < 0) != (q < 0))); /* Integer#/ floors */
            VALUE args[2] = {call0(ev, id_x), LONG2FIX(cur + 1)};
            VALUE tag = rb_funcallv(call0(ev, id_map), id_terrain_tag, 2, args);
            if (tagIs(tag, g_tags.stillWater) && RTEST(call0(self, id_visible)))
                return Qundef; /* shown: the Ruby method lays it out */
            if (!FIXNUM_P(tag) && g_tags.stillWater >= 0 &&
                RTEST(rb_funcall(tag, rb_intern("=="), 1, LONG2FIX(g_tags.stillWater))))
                return Qundef;
        }
    }
    VALUE spr = rb_ivar_get(self, iv_sprite);
    if (RTEST(spr)) {
        call0(spr, id_dispose);
        rb_ivar_set(self, iv_sprite, Qnil);
    }
    return Qnil;
}

VALUE nReflectedU(int argc, VALUE *, VALUE self) { return argc == 0 ? reflectedUpdate(self, V_URANIUM) : Qundef; }
VALUE nReflectedC(int argc, VALUE *, VALUE self) { return argc == 0 ? reflectedUpdate(self, V_CLASSIC) : Qundef; }

/* SpriteResizer. With $ResizeFactorMul == 100 every writer is the plain branch:
 *   x=: viewport ? _xeq(val) : _xeq(val + $ResizeOffsetX); @resizedX = val
 *   zoom_x=: _zoomxeq(val); @resizedZoomX = val
 *   ox=: (classic: only while $ResizeFactor == 1.0) @resizedOx = val; _oxeq(val)
 * Anything else runs the Ruby method. Readers return their ivar. */
bool factorIs100() {
    VALUE f = rb_gv_get("$ResizeFactorMul");
    return FIXNUM_P(f) && FIX2LONG(f) == 100;
}

VALUE posWrite(VALUE self, VALUE val, ID setter, const char *offsetVar, ID ivar) {
    if (!factorIs100())
        return Qundef;
    VALUE v = val;
    if (NIL_P(call0(self, id_viewport))) {
        VALUE off = rb_gv_get(offsetVar);
        v = (FIXNUM_P(val) && FIXNUM_P(off)) ? LONG2NUM(FIX2LONG(val) + FIX2LONG(off))
                                             : rb_funcall(val, id_plus, 1, off);
    }
    rb_funcallv(self, setter, 1, &v);
    rb_ivar_set(self, ivar, val);
    return val;
}

VALUE nXSet(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? posWrite(self, argv[0], id_xeq, "$ResizeOffsetX", iv_resizedX) : Qundef;
}
VALUE nYSet(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? posWrite(self, argv[0], id_yeq, "$ResizeOffsetY", iv_resizedY) : Qundef;
}

VALUE zoomWrite(VALUE self, VALUE val, ID setter, ID ivar) {
    if (!factorIs100())
        return Qundef;
    rb_funcallv(self, setter, 1, &val);
    rb_ivar_set(self, ivar, val);
    return val;
}

VALUE nZoomXSet(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? zoomWrite(self, argv[0], id_zoomxeq, iv_resizedZoomX) : Qundef;
}
VALUE nZoomYSet(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? zoomWrite(self, argv[0], id_zoomyeq, iv_resizedZoomY) : Qundef;
}

VALUE originWrite(VALUE self, VALUE val, ID setter, ID ivar, bool classic) {
    if (classic) {
        VALUE f = rb_gv_get("$ResizeFactor");
        bool one = RB_FLOAT_TYPE_P(f) ? RFLOAT_VALUE(f) == 1.0 : (FIXNUM_P(f) && FIX2LONG(f) == 1);
        if (!one)
            return Qundef;
    }
    rb_ivar_set(self, ivar, val);
    rb_funcallv(self, setter, 1, &val);
    return val;
}

VALUE nOxSetU(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? originWrite(self, argv[0], id_oxeq, iv_resizedOx, false) : Qundef;
}
VALUE nOySetU(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? originWrite(self, argv[0], id_oyeq, iv_resizedOy, false) : Qundef;
}
VALUE nOxSetC(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? originWrite(self, argv[0], id_oxeq, iv_resizedOx, true) : Qundef;
}
VALUE nOySetC(int argc, VALUE *argv, VALUE self) {
    return argc == 1 ? originWrite(self, argv[0], id_oyeq, iv_resizedOy, true) : Qundef;
}

VALUE nX(int argc, VALUE *, VALUE self) { return argc == 0 ? rb_ivar_get(self, iv_resizedX) : Qundef; }
VALUE nY(int argc, VALUE *, VALUE self) { return argc == 0 ? rb_ivar_get(self, iv_resizedY) : Qundef; }
VALUE nOx(int argc, VALUE *, VALUE self) { return argc == 0 ? rb_ivar_get(self, iv_resizedOx) : Qundef; }
VALUE nOy(int argc, VALUE *, VALUE self) { return argc == 0 ? rb_ivar_get(self, iv_resizedOy) : Qundef; }
VALUE nZoomX(int argc, VALUE *, VALUE self) { return argc == 0 ? rb_ivar_get(self, iv_resizedZoomX) : Qundef; }
VALUE nZoomY(int argc, VALUE *, VALUE self) { return argc == 0 ? rb_ivar_get(self, iv_resizedZoomY) : Qundef; }

/* RPG::Sprite#x= / #y= (Essentials' RPG::Sprite): move the battle animations along, then super.
 *   sx = x - self.x            (y=: sy = y - self.y; unused, but it is evaluated)
 *   for a in @animations; a.x=x if a; end     (y=: a.x=x too, x being self.x: the text's own slip)
 *   for a in @loopAnimations; a.x=x if a; end
 *   super */
inline bool isNum(VALUE v) { return FIXNUM_P(v) || RB_FLOAT_TYPE_P(v); }

VALUE rpgSpritePos(VALUE self, int argc, VALUE *argv, bool isY) {
    if (argc != 1)
        return Qundef;
    VALUE v = argv[0];
    if (!isNum(v) || !isNum(call0(self, isY ? id_y : id_x)))
        return Qundef; /* `v - self.x` would be another class's `-` */
    VALUE lists[2] = {rb_ivar_get(self, iv_animations), rb_ivar_get(self, iv_loopAnimations)};
    if (!RB_TYPE_P(lists[0], T_ARRAY) || !RB_TYPE_P(lists[1], T_ARRAY))
        return Qundef;
    for (VALUE list : lists) {
        for (long i = 0; i < RARRAY_LEN(list); ++i) {
            VALUE a = RARRAY_AREF(list, i);
            if (!RTEST(a))
                continue;
            VALUE nx = isY ? call0(self, id_x) : v;
            rb_funcallv(a, id_xeq_plain, 1, &nx);
        }
    }
    return rb_call_super(1, &v);
}

VALUE nRpgSpriteX(int argc, VALUE *argv, VALUE self) { return rpgSpritePos(self, argc, argv, false); }
VALUE nRpgSpriteY(int argc, VALUE *argv, VALUE self) { return rpgSpritePos(self, argc, argv, true); }

/* One-liners every frame leans on: Game_Character#map `(@map) ? @map : $game_map`,
 * Game_Map#data `@map.data`, PokemonSystem#tilemap `MAPVIEWMODE`. */
VALUE nCharMap(int argc, VALUE *, VALUE self) {
    if (argc != 0)
        return Qundef;
    VALUE m = rb_ivar_get(self, iv_map);
    return RTEST(m) ? m : rb_gv_get("$game_map");
}
VALUE nMapData(int argc, VALUE *, VALUE self) {
    return argc == 0 ? call0(rb_ivar_get(self, iv_map), id_data) : Qundef;
}
VALUE nTilemap(int argc, VALUE *, VALUE self) {
    if (argc != 0)
        return Qundef;
    return rb_const_get(rb_obj_class(self), id_MAPVIEWMODE);
}

/* Integer arithmetic as Ruby does it, Ruby's own operator for anything else. */
VALUE binop(VALUE a, char op, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b)) {
        long x = FIX2LONG(a), y = FIX2LONG(b);
        switch (op) {
        case '+': return LONG2NUM(x + y);
        case '-': return LONG2NUM(x - y);
        case '*':
            if (x > -32768 && x < 32768 && y > -32768 && y < 32768)
                return LONG2NUM(x * y);
            break;
        case '/':
            if (y != 0)
                return LONG2NUM(x / y - ((x % y != 0) && ((x < 0) != (y < 0))));
            break;
        }
    }
    char name[2] = {op, 0};
    return rb_funcall(a, rb_intern(name), 1, b);
}

inline bool neq(VALUE a, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return a != b;
    if (RB_TYPE_P(a, T_STRING) && RB_TYPE_P(b, T_STRING))
        return !RTEST(rb_str_equal(a, b));
    return RTEST(rb_funcall(a, rb_intern("!="), 1, b));
}

inline void set1(VALUE o, ID setter, VALUE v) { rb_funcallv(o, setter, 1, &v); }

/* Sprite_Character#update, Pokemon Uranium 1.3 text. The branch that loads a new graphic (when
 * the tile, name, hue or bush depth changed) is the game's own code, run as the helper
 * __hardrpg_sc_changed that essentials_natives.rb defines from the same lines. */
VALUE scUpdateU(int argc, VALUE *, VALUE self) {
    if (argc != 0)
        return Qundef;
    rb_call_super(0, nullptr);
    VALUE ch = rb_ivar_get(self, iv_character);
    if (neq(rb_ivar_get(self, iv_tile_id), call0(ch, id_tile_id)) ||
        neq(rb_ivar_get(self, iv_character_name), call0(ch, id_character_name)) ||
        neq(rb_ivar_get(self, iv_character_hue), call0(ch, id_character_hue)) ||
        neq(rb_ivar_get(self, iv_oldbushdepth), call0(ch, id_bush_depth)))
        call0(self, id_sc_changed);
    bool animated = RTEST(rb_ivar_get(self, iv_charbitmapAnimated));
    VALUE cb = rb_ivar_get(self, iv_charbitmap);
    if (animated)
        call0(cb, id_update);
    VALUE bushdepth = call0(ch, id_bush_depth);
    bool zero = FIXNUM_P(bushdepth) ? FIX2LONG(bushdepth) == 0
                                    : RTEST(rb_funcall(bushdepth, rb_intern("=="), 1, INT2FIX(0)));
    if (zero) {
        set1(self, id_bitmap_set, animated ? call0(cb, id_bitmap) : cb);
    } else {
        VALUE bb = rb_ivar_get(self, iv_bushbitmap);
        if (!RTEST(bb)) {
            VALUE tile = rb_ivar_get(self, iv_tile_id);
            VALUE big = (FIXNUM_P(tile) ? FIX2LONG(tile) >= 384 : RTEST(rb_funcall(tile, rb_intern(">="), 1, INT2FIX(384))))
                            ? Qtrue : Qfalse;
            VALUE args[3] = {cb, big, bushdepth};
            bb = rb_class_new_instance(3, args, rb_const_get(rb_cObject, rb_intern("BushBitmap")));
            rb_ivar_set(self, iv_bushbitmap, bb);
        }
        set1(self, id_bitmap_set, call0(bb, id_bitmap));
    }
    set1(self, id_visible_set, RTEST(call0(ch, id_transparent)) ? Qfalse : Qtrue);
    VALUE tile = rb_ivar_get(self, iv_tile_id);
    VALUE cw = rb_ivar_get(self, iv_cw), chh = rb_ivar_get(self, iv_ch);
    if (FIXNUM_P(tile) ? FIX2LONG(tile) == 0 : RTEST(rb_funcall(tile, rb_intern("=="), 1, INT2FIX(0)))) {
        VALUE sx = binop(call0(ch, id_pattern), '*', cw);
        VALUE sy = binop(binop(binop(call0(ch, id_direction), '-', INT2FIX(2)), '/', INT2FIX(2)), '*', chh);
        VALUE r[4] = {sx, sy, cw, chh};
        rb_funcallv(call0(self, id_src_rect), id_set, 4, r);
        set1(self, id_oy_set, RTEST(rb_ivar_get(self, iv_spriteoffset)) ? binop(chh, '-', INT2FIX(16)) : chh);
        set1(self, id_oy_set, binop(call0(self, id_oy), '-', call0(ch, id_bob_height)));
    }
    if (RTEST(call0(self, id_visible))) {
        VALUE tm = call0(rb_gv_get("$PokemonSystem"), id_tilemap);
        bool plain = FIXNUM_P(tm) ? FIX2LONG(tm) == 0 : RTEST(rb_funcall(tm, rb_intern("=="), 1, INT2FIX(0)));
        if (!plain && RTEST(rb_obj_is_kind_of(ch, rb_const_get(rb_cObject, rb_intern("Game_Event"))))) {
            VALUE re = g_regularTone;
            plain = RTEST(rb_funcall(call0(ch, id_name), id_aref, 1, re));
        }
        if (plain) {
            VALUE z[4] = {INT2FIX(0), INT2FIX(0), INT2FIX(0), INT2FIX(0)};
            rb_funcallv(call0(self, id_tone), id_set, 4, z);
        } else {
            rb_funcall(self, id_pbDayNightTint, 1, self);
        }
    }
    set1(self, id_x_set, call0(ch, id_screen_x));
    set1(self, id_y_set, call0(ch, id_screen_y));
    set1(self, id_z_set, rb_funcallv(ch, id_screen_z, 1, &chh));
    VALUE gm = rb_const_get(rb_cObject, rb_intern("Game_Map"));
    VALUE tw = rb_const_get(gm, rb_intern("TILEWIDTH")), th = rb_const_get(gm, id_TILEHEIGHT);
    VALUE f32 = DBL2NUM(32.0);
    set1(self, id_zoom_x_set, FIXNUM_P(tw) ? DBL2NUM(FIX2LONG(tw) / 32.0) : rb_funcall(tw, rb_intern("/"), 1, f32));
    set1(self, id_zoom_y_set, FIXNUM_P(th) ? DBL2NUM(FIX2LONG(th) / 32.0) : rb_funcall(th, rb_intern("/"), 1, f32));
    set1(self, id_opacity_set, call0(ch, id_opacity));
    set1(self, id_blend_type_set, call0(ch, id_blend_type));
    VALUE aid = call0(ch, id_animation_id);
    if (FIXNUM_P(aid) ? FIX2LONG(aid) != 0 : RTEST(rb_funcall(aid, rb_intern("!="), 1, INT2FIX(0)))) {
        VALUE anim = rb_funcall(rb_gv_get("$data_animations"), id_aref, 1, aid);
        rb_funcall(self, rb_intern("animation"), 2, anim, Qtrue);
        set1(ch, rb_intern("animation_id="), INT2FIX(0));
        return INT2FIX(0);
    }
    return Qnil;
}

/* ------------------------------------------------------------------ */
/* Day/night tint and weather (Pokemon Uranium 1.3 texts). */

inline VALUE aref1(VALUE a, VALUE i) {
    if (RB_TYPE_P(a, T_ARRAY) && FIXNUM_P(i))
        return rb_ary_entry(a, FIX2LONG(i));
    return rb_funcall(a, id_aref, 1, i);
}

/* a <op> b for '<', '>', '>=', '==' as Ruby compares them. */
inline bool cmp(VALUE a, const char *op, VALUE b) {
    if (FIXNUM_P(a) && FIXNUM_P(b)) {
        long x = FIX2LONG(a), y = FIX2LONG(b);
        switch (op[0]) {
        case '<': return x < y;
        case '>': return op[1] == '=' ? x >= y : x > y;
        case '=': return x == y;
        }
    }
    return RTEST(rb_funcall(a, rb_intern(op), 1, b));
}

inline VALUE neg(VALUE a) {
    return FIXNUM_P(a) ? LONG2NUM(-FIX2LONG(a)) : rb_funcall(a, rb_intern("-@"), 0);
}

/* pbLoadMetadata: the loaded table, or the Ruby text when it still has to load it. */
VALUE nLoadMeta(int argc, VALUE *, VALUE) {
    if (argc != 0)
        return Qundef;
    VALUE pt = rb_gv_get("$PokemonTemp");
    if (!RTEST(pt))
        return Qundef;
    VALUE m = call0(pt, rb_intern("pokemonMetadata"));
    return RTEST(m) ? m : Qundef;
}

/* pbGetMetadata(mapid, type): `meta=pbLoadMetadata; return meta[mapid][type] if meta[mapid]; nil` */
VALUE nGetMeta(int argc, VALUE *argv, VALUE self) {
    if (argc != 2)
        return Qundef;
    VALUE meta = rb_funcall(self, rb_intern("pbLoadMetadata"), 0);
    if (RTEST(aref1(meta, argv[0])))
        return aref1(aref1(meta, argv[0]), argv[1]);
    return Qnil;
}

/* PBDayNight.getTone: the cached tone while it is fresh; Ruby recomputes it every 30 s. */
VALUE nGetToneU(int argc, VALUE *, VALUE self) {
    if (argc != 0)
        return Qundef;
    VALUE ct = rb_ivar_get(self, rb_intern("@cachedTone"));
    if (!RTEST(ct))
        return Qundef;
    if (!RTEST(rb_const_get(self, rb_intern("ENABLESHADING"))))
        return ct;
    VALUE last = rb_ivar_get(self, rb_intern("@dayNightToneLastUpdate"));
    if (!RTEST(last))
        return Qundef;
    VALUE fc = call0(g_graphics, rb_intern("frame_count"));
    VALUE fr = call0(g_graphics, rb_intern("frame_rate"));
    if (!FIXNUM_P(fc) || !FIXNUM_P(last) || !FIXNUM_P(fr))
        return Qundef;
    if (FIX2LONG(fc) - FIX2LONG(last) >= FIX2LONG(fr) * 30)
        return Qundef;
    return ct;
}

inline VALUE fdiv175(VALUE a) {
    return RB_FLOAT_TYPE_P(a) ? DBL2NUM(RFLOAT_VALUE(a) / 1.75) : rb_funcall(a, rb_intern("/"), 1, DBL2NUM(1.75));
}

/* pbDayNightTint(object), Uranium text (LESSDARKMAPS dims the tone by 1.75). */
VALUE nTintU(int argc, VALUE *argv, VALUE self) {
    if (argc != 1)
        return Qundef;
    VALUE obj = argv[0];
    if (!RTEST(rb_obj_is_kind_of(rb_gv_get("$scene"), rb_const_get(rb_cObject, rb_intern("Scene_Map")))))
        return Qnil;
    bool outdoor = false;
    VALUE gm = Qnil;
    if (RTEST(rb_const_get(rb_cObject, rb_intern("ENABLESHADING")))) {
        gm = rb_gv_get("$game_map");
        if (RTEST(gm)) {
            VALUE a[2] = {call0(gm, rb_intern("map_id")), rb_const_get(rb_cObject, rb_intern("MetadataOutdoor"))};
            outdoor = RTEST(rb_funcallv(self, rb_intern("pbGetMetadata"), 2, a));
        }
    }
    VALUE v[4];
    if (outdoor) {
        VALUE tone = call0(rb_const_get(rb_cObject, rb_intern("PBDayNight")), rb_intern("getTone"));
        VALUE less = rb_funcall(rb_const_get(rb_cObject, rb_intern("LESSDARKMAPS")), rb_intern("include?"), 1,
                                call0(gm, rb_intern("map_id")));
        VALUE t = call0(obj, id_tone);
        v[0] = call0(tone, rb_intern("red"));
        v[1] = call0(tone, rb_intern("green"));
        v[2] = call0(tone, rb_intern("blue"));
        v[3] = call0(tone, rb_intern("gray"));
        if (RTEST(less))
            for (VALUE &x : v)
                x = fdiv175(x);
        return rb_funcallv(t, id_set, 4, v);
    }
    VALUE t = call0(obj, id_tone);
    v[0] = v[1] = v[2] = v[3] = INT2FIX(0);
    return rb_funcallv(t, id_set, 4, v);
}

/* RPG::Weather#update, Uranium text. */
VALUE wUpdateU(int argc, VALUE *, VALUE self) {
    if (argc != 0)
        return Qundef;
    VALUE type = rb_ivar_get(self, rb_intern("@type"));
    VALUE max = rb_ivar_get(self, rb_intern("@max"));
    /* @max is a Float while the weather fades (Game_Screen eases weather_max). */
    if (!FIXNUM_P(type) || !(FIXNUM_P(max) || RB_FLOAT_TYPE_P(max)))
        return Qundef;
    long t = FIX2LONG(type);
    long n = 0; /* `for i in 1..@max` runs 1..floor(@max) */
    if (FIXNUM_P(max)) {
        n = FIX2LONG(max);
    } else {
        double d = RFLOAT_VALUE(max);
        if (!(d > -1e9 && d < 1e9))
            return Qundef;
        n = (long)floor(d);
        if (t == 5)
            return Qundef;
    }
    VALUE wt = Qnil;
    if (t != 0 && t != 5) {
        /* everything the sprite loop reads, checked before anything changes */
        VALUE types = rb_ivar_get(self, rb_intern("@weatherTypes"));
        wt = RB_TYPE_P(types, T_ARRAY) ? rb_ary_entry(types, t) : Qnil;
        if (!RB_TYPE_P(wt, T_ARRAY) || RARRAY_LEN(wt) < 4)
            return Qundef;
    } else if (t == 5) {
        if (!FIXNUM_P(rb_ivar_get(self, rb_intern("@sun"))) || !FIXNUM_P(rb_ivar_get(self, rb_intern("@sunvalue"))))
            return Qundef;
    }
    VALUE vp = rb_ivar_get(self, rb_intern("@viewport"));
    auto toneSet = [&](VALUE a, VALUE b, VALUE c, VALUE d) {
        VALUE tn = call0(vp, id_tone);
        VALUE v[4] = {a, b, c, d};
        rb_funcallv(tn, id_set, 4, v);
    };
    auto muldiv = [](VALUE a, long m, long d) { return binop(binop(a, '*', LONG2NUM(m)), '/', LONG2NUM(d)); };
    VALUE z = INT2FIX(0);
    switch (t) {
    case 0: toneSet(z, z, z, z); break;
    case 1: toneSet(muldiv(neg(max), 3, 5), muldiv(neg(max), 3, 5), muldiv(neg(max), 3, 5), INT2FIX(10)); break;
    case 2:
    case 6: toneSet(muldiv(neg(max), 6, 5), muldiv(neg(max), 6, 5), muldiv(neg(max), 6, 5), INT2FIX(20)); break;
    case 3: toneSet(muldiv(max, 2, 4), muldiv(max, 2, 4), muldiv(max, 2, 4), z); break;
    case 4: toneSet(muldiv(max, 2, 4), z, muldiv(neg(max), 2, 4), z); break;
    case 5: {
        ID ivSun = rb_intern("@sun"), ivSv = rb_intern("@sunvalue");
        long m = FIX2LONG(max);
        long sun = FIX2LONG(rb_ivar_get(self, ivSun));
        if (!(sun == m || sun == -m))
            sun = m;
        long sv = FIX2LONG(rb_ivar_get(self, ivSv));
        if (sv > m || sv < 0)
            sun = -sun;
        rb_ivar_set(self, ivSun, LONG2NUM(sun));
        VALUE nsv = binop(LONG2NUM(sv), '+', binop(LONG2NUM(sun), '/', INT2FIX(32)));
        rb_ivar_set(self, ivSv, nsv);
        toneSet(binop(nsv, '+', INT2FIX(63)), binop(nsv, '+', INT2FIX(63)),
                binop(binop(nsv, '/', INT2FIX(2)), '+', INT2FIX(31)), z);
        break;
    }
    case 7: toneSet(muldiv(max, 3, 4), muldiv(max, 3, 4), muldiv(max, 3, 4), z); break;
    case 8: toneSet(z, max, z, z); break;
    }
    ID id_rand = rb_intern("rand");
    if (t == 2) {
        VALUE rnd = rb_funcall(self, id_rand, 1, INT2FIX(300));
        if (cmp(rnd, "<", INT2FIX(2))) {
            VALUE ca[4] = {INT2FIX(255), INT2FIX(255), INT2FIX(255), INT2FIX(230)};
            VALUE col = rb_class_new_instance(4, ca, rb_const_get(rb_cObject, rb_intern("Color")));
            rb_funcall(vp, rb_intern("flash"), 2, col, binop(rnd, '*', INT2FIX(20)));
            if (cmp(rnd, "<", INT2FIX(2)))
                rb_funcall(self, rb_intern("pbSEPlay"), 2, rb_str_new_cstr("061-Thunderclap01"), INT2FIX(80));
        }
    }
    call0(vp, id_update);
    if (t == 0 || t == 5)
        return Qnil;
    call0(self, rb_intern("ensureSprites"));
    VALUE dx = rb_ary_entry(wt, 1), dy = rb_ary_entry(wt, 2), dop = rb_ary_entry(wt, 3);
    ID ivSprites = rb_intern("@sprites"), ivOx = rb_intern("@ox"), ivOy = rb_intern("@oy");
    ID id_mirror_set = rb_intern("mirror=");
    VALUE nw = call0(g_graphics, id_width), nh = call0(g_graphics, id_height);
    for (long i = 1; i <= n; ++i) {
        VALUE s = aref1(rb_ivar_get(self, ivSprites), LONG2FIX(i));
        if (NIL_P(s))
            break;
        set1(s, id_x_set, binop(call0(s, id_x), '+', dx));
        set1(s, id_y_set, binop(call0(s, id_y), '+', dy));
        set1(s, id_opacity_set, binop(call0(s, id_opacity), '+', dop));
        if (t == 3 || t == 7) {
            static const long jit[4] = {2, 0, 0, -2};
            VALUE r = rb_funcall(self, id_rand, 1, INT2FIX(4));
            VALUE j = FIXNUM_P(r) && FIX2LONG(r) >= 0 && FIX2LONG(r) < 4 ? LONG2FIX(jit[FIX2LONG(r)])
                                                                          : Qnil;
            set1(s, id_x_set, binop(call0(s, id_x), '+', j));
        }
        VALUE x = binop(call0(s, id_x), '-', rb_ivar_get(self, ivOx));
        VALUE y = binop(call0(s, id_y), '-', rb_ivar_get(self, ivOy));
        if (cmp(call0(s, id_opacity), "<", INT2FIX(64)) || cmp(x, "<", INT2FIX(-50)) ||
            cmp(x, ">", binop(nw, '+', INT2FIX(128))) || cmp(y, "<", INT2FIX(-300)) ||
            cmp(y, ">", binop(nh, '+', INT2FIX(20)))) {
            VALUE rx = rb_funcall(self, id_rand, 1, binop(nw, '+', INT2FIX(150)));
            set1(s, id_x_set, binop(binop(rx, '-', INT2FIX(50)), '+', rb_ivar_get(self, ivOx)));
            VALUE ry = rb_funcall(self, id_rand, 1, binop(nh, '+', INT2FIX(150)));
            set1(s, id_y_set, binop(binop(ry, '-', INT2FIX(200)), '+', rb_ivar_get(self, ivOy)));
            set1(s, id_opacity_set, INT2FIX(255));
            if (t == 4)
                set1(s, id_mirror_set, cmp(rb_funcall(self, id_rand, 1, INT2FIX(2)), "==", INT2FIX(0)) ? Qtrue : Qfalse);
            else
                set1(s, id_mirror_set, Qfalse);
        }
        rb_funcall(self, id_pbDayNightTint, 1, s);
    }
    return Qnil;
}

/* ------------------------------------------------------------------ */
/* The table. Each entry keeps the Ruby method it replaced, for the slow path and for restore. */

struct NativeDef {
    const char *key;     /* "Class#method" */
    const char *variant; /* the text it matches (essentials_natives.rb KNOWN) */
    NativeFn fn;
};

const NativeDef g_natives[] = {
    {"Game_Map#terrain_tag", "uranium", nTerrainU},
    {"Game_Map#terrain_tag", "classic", nTerrainC},
    {"Game_Map#bush?", "uranium", nBush},
    {"Game_Map#bush?", "classic", nBush},
    {"Game_Map#deepBush?", "uranium", nDeepBush},
    {"Game_Character#bush_depth", "uranium", nBushDepthU},
    {"Game_Character#bush_depth", "classic", nBushDepthC},
    {"Spriteset_Map#in_range?", "uranium", nSpritesetInRangeU},
    {"Spriteset_Map#in_range?", "classic", nSpritesetInRangeC},
    {"Game_Map#in_range?", "uranium", nMapInRangeU},
    {"Game_Map#in_range?", "classic", nMapInRangeC},
    {"ReflectedSprite#update", "uranium", nReflectedU},
    {"ReflectedSprite#update", "classic", nReflectedC},
    {"Sprite#x=", "resizer", nXSet},
    {"Sprite#y=", "resizer", nYSet},
    {"Sprite#zoom_x=", "resizer", nZoomXSet},
    {"Sprite#zoom_y=", "resizer", nZoomYSet},
    {"Sprite#ox=", "uranium", nOxSetU},
    {"Sprite#oy=", "uranium", nOySetU},
    {"Sprite#ox=", "classic", nOxSetC},
    {"Sprite#oy=", "classic", nOySetC},
    {"Sprite#x", "resizer", nX},
    {"Sprite#y", "resizer", nY},
    {"Sprite#ox", "resizer", nOx},
    {"Sprite#oy", "resizer", nOy},
    {"Sprite#zoom_x", "resizer", nZoomX},
    {"Sprite#zoom_y", "resizer", nZoomY},
    {"RPG::Sprite#x=", "default", nRpgSpriteX},
    {"RPG::Sprite#y=", "default", nRpgSpriteY},
    {"Game_Character#map", "default", nCharMap},
    {"Game_Map#data", "default", nMapData},
    {"PokemonSystem#tilemap", "default", nTilemap},
    {"Sprite_Character#update", "uranium", scUpdateU},
    {"Object#pbLoadMetadata", "default", nLoadMeta},
    {"Object#pbGetMetadata", "default", nGetMeta},
    {"PBDayNight.self#getTone", "uranium", nGetToneU},
    {"Object#pbDayNightTint", "uranium", nTintU},
    {"RPG::Weather#update", "uranium", wUpdateU},
};
const int kNatives = sizeof(g_natives) / sizeof(g_natives[0]);

VALUE g_orig[kNatives];
ID g_origId[kNatives]; /* the replaced Ruby method, kept as __hardrpg_<i>_<name> */
unsigned long g_calls[kNatives], g_slow[kNatives];

VALUE slowPath(int i, VALUE self, int argc, VALUE *argv) {
    ++g_slow[i];
    return rb_funcallv(self, g_origId[i], argc, argv);
}

/* One C entry point per table slot (rb_define_method passes no context). */
template <int I>
VALUE entry(int argc, VALUE *argv, VALUE self) {
    ++g_calls[I];
    VALUE r = g_natives[I].fn(argc, argv, self);
    return r == Qundef ? slowPath(I, self, argc, argv) : r;
}

const NativeFn g_entries[] = {
    entry<0>, entry<1>, entry<2>, entry<3>, entry<4>, entry<5>, entry<6>, entry<7>, entry<8>,
    entry<9>, entry<10>, entry<11>, entry<12>, entry<13>, entry<14>, entry<15>, entry<16>,
    entry<17>, entry<18>, entry<19>, entry<20>, entry<21>, entry<22>, entry<23>, entry<24>,
    entry<25>, entry<26>, entry<27>, entry<28>, entry<29>, entry<30>, entry<31>, entry<32>,
    entry<33>, entry<34>, entry<35>, entry<36>, entry<37>,
};
static_assert(sizeof(g_entries) / sizeof(g_entries[0]) == kNatives, "one entry per native");

int findNative(const char *key, const char *variant) {
    for (int i = 0; i < kNatives; ++i)
        if (!strcmp(g_natives[i].key, key) && !strcmp(g_natives[i].variant, variant))
            return i;
    return -1;
}

int g_active[kNatives]; /* 1 while the slot's native is the method in place */

/* HardRPGEssentialsNative.replace(klass, "Class#method", variant) -> true when swapped. */
VALUE nevReplace(VALUE, VALUE klass, VALUE key, VALUE variant) {
    int i = findNative(StringValueCStr(key), StringValueCStr(variant));
    if (i < 0)
        return Qfalse;
    const char *name = strchr(g_natives[i].key, '#') + 1;
    g_orig[i] = rb_funcall(klass, rb_intern("instance_method"), 1, ID2SYM(rb_intern(name)));
    char alias[96];
    snprintf(alias, sizeof alias, "__hardrpg_%d_%s", i, name);
    g_origId[i] = rb_intern(alias);
    rb_alias(klass, g_origId[i], rb_intern(name));
    g_tags.ready = false; /* re-read constants when first used */
    if (RTEST(rb_funcall(klass, rb_intern("private_method_defined?"), 1, ID2SYM(rb_intern(name)))))
        rb_define_private_method(klass, name, RUBY_METHOD_FUNC(g_entries[i]), -1);
    else
        rb_define_method(klass, name, RUBY_METHOD_FUNC(g_entries[i]), -1);
    g_active[i] = 1;
    return Qtrue;
}

/* HardRPGEssentialsNative.restore(klass, "Class#method"): put the kept Ruby method back. */
VALUE nevRestore(VALUE, VALUE klass, VALUE key) {
    const char *k = StringValueCStr(key);
    for (int i = 0; i < kNatives; ++i) {
        if (!g_active[i] || strcmp(g_natives[i].key, k))
            continue;
        const char *name = strchr(k, '#') + 1;
        rb_funcall(klass, rb_intern("define_method"), 2, ID2SYM(rb_intern(name)), g_orig[i]);
        g_active[i] = 0;
        return Qtrue;
    }
    return Qfalse;
}

/* HardRPGEssentialsNative.stats -> {"Class#method/variant" => [calls, slow], ...} (used ones) */
VALUE nevStats(VALUE) {
    VALUE h = rb_hash_new();
    for (int i = 0; i < kNatives; ++i) {
        if (!g_calls[i] && !g_active[i])
            continue;
        VALUE k = rb_sprintf("%s/%s", g_natives[i].key, g_natives[i].variant);
        rb_hash_aset(h, k, rb_ary_new_from_args(2, ULONG2NUM(g_calls[i]), ULONG2NUM(g_slow[i])));
    }
    return h;
}

/* HardRPGEssentialsNative.variants(key) -> variants the engine has for "Class#method". */
VALUE nevVariants(VALUE, VALUE key) {
    VALUE a = rb_ary_new();
    const char *k = StringValueCStr(key);
    for (int i = 0; i < kNatives; ++i)
        if (!strcmp(g_natives[i].key, k))
            rb_ary_push(a, rb_str_new_cstr(g_natives[i].variant));
    return a;
}

} // namespace

void essentialsNativesInit(int rgssVersion) {
    if (rgssVersion != 1)
        return;
#define I(v, s) v = rb_intern(s)
    I(iv_map_id, "@map_id"); I(iv_terrain_tags, "@terrain_tags"); I(iv_passages, "@passages");
    I(id_data, "data"); I(id_bridge, "bridge"); I(id_aref, "[]"); I(id_Bridge, "Bridge");
    I(id_Shadow, "Shadow"); I(id_TallGrass, "TallGrass"); I(id_StillWater, "StillWater");
    I(id_TILEHEIGHT, "TILEHEIGHT"); I(iv_tile_id, "@tile_id"); I(iv_always_on_top, "@always_on_top");
    I(iv_jump_count, "@jump_count"); I(iv_direction, "@direction"); I(iv_x, "@x"); I(iv_y, "@y");
    I(id_map, "map"); I(id_bush, "bush?"); I(id_deepBush, "deepBush?"); I(id_moving, "moving?");
    I(id_terrain_tag, "terrain_tag"); I(id_display_x, "display_x"); I(id_display_y, "display_y"); I(id_tilemap, "tilemap");
    I(id_real_x, "real_x"); I(id_real_y, "real_y"); I(id_width, "width"); I(id_height, "height");
    I(id_x, "x"); I(id_y, "y"); I(id_xeq_plain, "x="); I(iv_animations, "@animations");
    I(iv_loopAnimations, "@loopAnimations"); I(id_visible, "visible"); I(id_disposed, "disposed?"); I(id_dispose, "dispose");
    I(id_src_rect, "src_rect"); I(id_to_i, "to_i"); I(id_viewport, "viewport"); I(id_plus, "+");
    I(iv_map, "@map"); I(iv_event, "@event"); I(iv_rsprite, "@rsprite"); I(iv_sprite, "@sprite");
    I(iv_resizedX, "@resizedX"); I(iv_resizedY, "@resizedY"); I(iv_resizedOx, "@resizedOx");
    I(iv_resizedOy, "@resizedOy"); I(iv_resizedZoomX, "@resizedZoomX"); I(iv_resizedZoomY, "@resizedZoomY");
    I(id_xeq, "_xeq_SpriteResizer"); I(id_yeq, "_yeq_SpriteResizer"); I(id_oxeq, "_oxeq_SpriteResizer");
    I(id_oyeq, "_oyeq_SpriteResizer"); I(id_zoomxeq, "_zoomxeq_SpriteResizer");
    I(id_zoomyeq, "_zoomyeq_SpriteResizer");
    I(id_MAPVIEWMODE, "MAPVIEWMODE"); I(iv_character, "@character"); I(iv_character_name, "@character_name");
    I(iv_character_hue, "@character_hue"); I(iv_oldbushdepth, "@oldbushdepth");
    I(iv_charbitmapAnimated, "@charbitmapAnimated"); I(iv_charbitmap, "@charbitmap");
    I(iv_bushbitmap, "@bushbitmap"); I(iv_cw, "@cw"); I(iv_ch, "@ch"); I(iv_spriteoffset, "@spriteoffset");
    I(id_tile_id, "tile_id"); I(id_character_name, "character_name"); I(id_character_hue, "character_hue");
    I(id_bush_depth, "bush_depth"); I(id_sc_changed, "__hardrpg_sc_changed"); I(id_update, "update");
    I(id_bitmap, "bitmap"); I(id_bitmap_set, "bitmap="); I(id_visible_set, "visible=");
    I(id_transparent, "transparent"); I(id_pattern, "pattern"); I(id_direction, "direction");
    I(id_set, "set"); I(id_oy_set, "oy="); I(id_oy, "oy"); I(id_bob_height, "bob_height"); I(id_name, "name");
    I(id_tone, "tone"); I(id_pbDayNightTint, "pbDayNightTint"); I(id_x_set, "x="); I(id_y_set, "y=");
    I(id_z_set, "z="); I(id_screen_x, "screen_x"); I(id_screen_y, "screen_y"); I(id_screen_z, "screen_z");
    I(id_zoom_x_set, "zoom_x="); I(id_zoom_y_set, "zoom_y="); I(id_opacity_set, "opacity=");
    I(id_opacity, "opacity"); I(id_blend_type_set, "blend_type="); I(id_blend_type, "blend_type");
    I(id_animation_id, "animation_id");
#undef I
    g_regularTone = rb_reg_new("RegularTone", 11, 0);
    rb_gc_register_mark_object(g_regularTone);
    for (int i = 0; i < kNatives; ++i) {
        g_orig[i] = Qnil;
        rb_gc_register_address(&g_orig[i]);
    }
    g_graphics = rb_path2class("Graphics");
    VALUE m = rb_define_module("HardRPGEssentialsNative");
    rb_define_singleton_method(m, "replace", RUBY_METHOD_FUNC(nevReplace), 3);
    rb_define_singleton_method(m, "restore", RUBY_METHOD_FUNC(nevRestore), 2);
    rb_define_singleton_method(m, "stats", RUBY_METHOD_FUNC(nevStats), 0);
    rb_define_singleton_method(m, "variants", RUBY_METHOD_FUNC(nevVariants), 1);
}
