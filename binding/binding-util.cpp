#include "frame_profile.h"
/*
** binding-util.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "binding-util.h"
#include "bitmap.h"

#include "exception.h"
#include "sharedstate.h"
#include "src/util/util.h"

#include <assert.h>
#include <stdarg.h>
#include <string.h>

#ifdef __vita__
#include "vita_error_report.h"

/* The game's image cache (VX/VX Ace `Cache`, XP `RPG::Cache`) keeps every
 * picture, face, battler and tileset it ever loaded, as live Bitmaps the
 * collector cannot touch. On a PC that is harmless; here a long session
 * fills graphics memory with images last shown an hour ago (issue 4: 144
 * entries, then a 640x480 texture at a battle start had nowhere to go).
 *
 * Forget the cached references, as the games' own Cache.clear does (BLACK
 * SOULS II calls it on every bonfire warp): bitmaps still in use stay alive
 * through their sprites and windows, the rest become collectable, and the
 * next request loads the file again. Bitmaps are never disposed here. The
 * game's Cache.clear itself is not called, since some games redefine it to
 * dispose. Returns the number of entries released. */
static VALUE vitaReleaseImageCacheBody(VALUE)
{
  const ID cacheIvar = rb_intern("@cache");
  VALUE owners[2] = {Qnil, Qnil};
  if (rb_const_defined(rb_cObject, rb_intern("Cache")))
    owners[0] = rb_const_get(rb_cObject, rb_intern("Cache"));
  if (rb_const_defined(rb_cObject, rb_intern("RPG"))) {
    VALUE rpg = rb_const_get(rb_cObject, rb_intern("RPG"));
    if ((RB_TYPE_P(rpg, T_MODULE) || RB_TYPE_P(rpg, T_CLASS)) &&
        rb_const_defined_at(rpg, rb_intern("Cache")))
      owners[1] = rb_const_get_at(rpg, rb_intern("Cache"));
  }
  long released = 0;
  for (VALUE owner : owners) {
    if (!(RB_TYPE_P(owner, T_MODULE) || RB_TYPE_P(owner, T_CLASS)))
      continue;
    if (!RTEST(rb_ivar_defined(owner, cacheIvar)))
      continue;
    VALUE cache = rb_ivar_get(owner, cacheIvar);
    if (!RB_TYPE_P(cache, T_HASH) || OBJ_FROZEN(cache))
      continue;
    released += RHASH_SIZE(cache);
    rb_hash_clear(cache);
  }
  return LONG2NUM(released);
}

/* Same call rules as vitaGpuPressureSafePoint. Never raises. */
long vitaReleaseGameImageCache()
{
  int state = 0;
  VALUE released = rb_protect(vitaReleaseImageCacheBody, Qnil, &state);
  if (state) {
    rb_set_errinfo(Qnil);
    return 0;
  }
  const long count = NUM2LONG(released);
  if (count > 0)
    vitaErrorMemoryEvent("image-cache-released", "Cache");
  return count;
}
#endif

#if defined(__vita__) && !defined(MKXPZ_VITA_DIAGNOSTICS)
#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>

static VALUE vitaPressureCollect(VALUE) { return rb_gc_start(); }
static VALUE vitaPressureCollectDeep(VALUE)
{
  /* Not reclaimMemory: it moves CPU bitmaps onto the GPU, which is short. */
  GFX_LOCK;
  Bitmap::releaseRecreatableMemory();
  GFX_UNLOCK;
  vitaReleaseGameImageCache();
  return rb_gc_start();
}

/* Graphics memory is invisible to Ruby's collector: a Bitmap is a few bytes
 * of Ruby heap holding megabytes of texture storage. Games routinely drop
 * bitmaps without disposing them (a sprite's dispose leaves its bitmap), so
 * the texture pools can run dry long before Ruby decides to collect. Collect
 * when the pools run low and have shrunk measurably since the last pass.
 *
 * Called only at Ruby C-method entry, before native pointers, GFX_LOCK or
 * drop_gvl_guard. Never collect from an upload, finalizer or allocator. */
void vitaGpuPressureSafePoint(const char *phase)
{
  static const size_t lowWater = 24u * 1024 * 1024;
  static const size_t step = 8u * 1024 * 1024;
  static const size_t exhausted = 2u * 1024 * 1024;
  static bool collecting = false;
  static size_t ceiling = 0; /* most free memory seen since the last pass */
  static unsigned int creations = 0;
  static uint64_t lastUs = 0;
  static unsigned int frames = 0;
  if (collecting) return;
  /* Last resort, used rarely: when a plain collection cannot lift the
   * pools, release the game's image cache too (see above). Also right after
   * a bitmap had to be skipped mid-draw for lack of memory. */
  static const size_t cacheFloor = 12u * 1024 * 1024;
  static uint64_t lastDeepUs = 0;
  /* A game that simply needs most of the memory must not reload its cache
   * every few seconds: each release that left the pools low doubles the
   * wait, up to 80 s; pools well above the floor reset it. */
  static uint64_t deepIntervalUs = 10000000;
  const bool skipped = Bitmap::takeReclaimRequest();
  if (skipped || (phase[0] == 'g' && !(frames & 7))) {
    const size_t poolFree = vglMemFree(VGL_MEM_VRAM) + vglMemFree(VGL_MEM_RAM);
    const uint64_t nowUs = sceKernelGetProcessTimeWide();
    if (poolFree >= cacheFloor + step)
      deepIntervalUs = 10000000;
    if (skipped || (poolFree < cacheFloor && nowUs - lastDeepUs >= deepIntervalUs)) {
      if (!skipped && lastDeepUs && deepIntervalUs < 80000000)
        deepIntervalUs *= 2;
      collecting = true;
      int state = 0;
      rb_protect(vitaPressureCollectDeep, Qnil, &state);
      collecting = false;
      lastDeepUs = lastUs = sceKernelGetProcessTimeWide();
      ceiling = vglMemFree(VGL_MEM_VRAM) + vglMemFree(VGL_MEM_RAM);
      creations = 0;
      if (state) rb_jump_tag(state);
      return;
    }
  }
  /* Reading the pool figures walks both allocators, so they are sampled on
   * every eighth frame and every fourth bitmap creation, not on each call. */
  if (phase[0] == 'b') {
    ++creations;
    /* CPU-composed bitmaps live in the C heap, which the pool figures below
     * do not see. Collect dropped ones before their budget fills. */
    /* A collection only helps if dropped bitmaps piled up since the last
     * one; a scene that simply keeps many live bitmaps must not pay for a
     * full collection every few seconds. */
    static size_t cpuAfterCollect = 0;
    if (Bitmap::cpuPixelsNearBudget() &&
        Bitmap::cpuPixelBytes() >= cpuAfterCollect + 8u * 1024 * 1024 &&
        sceKernelGetProcessTimeWide() - lastUs >= 2000000) {
      collecting = true;
      int state = 0;
      rb_protect(vitaPressureCollect, Qnil, &state);
      collecting = false;
      cpuAfterCollect = Bitmap::cpuPixelBytes();
      creations = 0;
      lastUs = sceKernelGetProcessTimeWide();
      if (state) rb_jump_tag(state);
      return;
    }
    if (Bitmap::cpuPixelBytes() < cpuAfterCollect)
      cpuAfterCollect = Bitmap::cpuPixelBytes();
    if (creations & 3) return;
  } else if (phase[0] == 'g' && (++frames & 7)) {
    return;
  }
  const size_t freeBytes = vglMemFree(VGL_MEM_VRAM) + vglMemFree(VGL_MEM_RAM);
  if (freeBytes > ceiling) ceiling = freeBytes;
  if (freeBytes >= lowWater) return;
  bool collect = ceiling - freeBytes >= (ceiling / 2 < step ? ceiling / 2 : step) &&
                 ceiling >= exhausted;
  if (!collect && freeBytes < exhausted) {
    /* Pools are spent and new storage is spilling into the C heap, where
     * the pool figures no longer move. Pace by bitmap creations instead. */
    collect = creations >= 8 &&
              sceKernelGetProcessTimeWide() - lastUs >= 2000000;
  }
  if (!collect) return;
  collecting = true;
  int state = 0;
  rb_protect(vitaPressureCollect, Qnil, &state);
  collecting = false;
  /* Released storage returns over the next few frames; measure from here. */
  ceiling = vglMemFree(VGL_MEM_VRAM) + vglMemFree(VGL_MEM_RAM);
  creations = 0;
  lastUs = sceKernelGetProcessTimeWide();
  if (state) rb_jump_tag(state);
}
#endif

#ifdef MKXPZ_VITA_DIAGNOSTICS
#include <vitaGL.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>

static VALUE vitaPressureCollect(VALUE) { return rb_gc_start(); }

void vitaGpuPressureSafePoint(const char *phase)
{
    FrameProfile::Scope profile(FrameProfile::HeapCheckpoint);
  /* Called only at Ruby C-method entry, before native pointers, GFX_LOCK,
   * or drop_gvl_guard. Never collect from an upload, finalizer or allocator.
   * Both experiment arms use this executable; only app0 marker differs. */
  static bool configured = false, enabled = false, collecting = false;
  static unsigned long long lastBytes = 0, lastUs = 0;
  static unsigned int checkpoints = 0;
  if (collecting) return;
  if (!configured) {
    FILE *marker = fopen("app0:/gpu-pressure-gc.on", "rb");
    enabled = marker != NULL;
    if (marker) fclose(marker);
    configured = true;
    vitaDiagLog("GPUGC", "config enabled=%d low_water_bytes=50331648 "
                "upload_interval_bytes=4194304 interval_us=1000000", enabled);
    vitaDiagGpuSnapshot("initial");
  }
  const unsigned long long freeBytes =
      (unsigned long long)vglMemFree(VGL_MEM_VRAM) + vglMemFree(VGL_MEM_RAM);
  if (freeBytes >= 48ULL * 1024 * 1024) return;
  const unsigned long long bytes = vitaDiagTextureUploadBytes();
  const unsigned long long now = sceKernelGetSystemTimeWide();
  if (checkpoints && bytes - lastBytes < 4ULL * 1024 * 1024 &&
      now - lastUs < 1000000) return;
  lastBytes = bytes;
  lastUs = now;
  ++checkpoints;
  vitaDiagLog("GPUGC", "checkpoint=%u phase=%s enabled=%d tid=%d", checkpoints,
              phase, enabled, (int)sceKernelGetThreadId());
  vitaDiagGpuSnapshot("before_gc");
  if (!enabled) return;
  collecting = true;
  int state = 0;
  rb_protect(vitaPressureCollect, Qnil, &state);
  collecting = false;
  vitaDiagGpuSnapshot("after_gc");
  vitaDiagLog("GPUGC", "complete checkpoint=%u elapsed_us=%llu ruby_state=%d",
              checkpoints, (unsigned long long)(sceKernelGetSystemTimeWide() - now), state);
  if (state) rb_jump_tag(state);
}
#endif

RbData *getRbData() { return static_cast<RbData *>(shState->bindingData()); }

struct {
  RbException id;
  const char *name;
} static customExc[] = {
    {MKXP, "MKXPError"}, {PHYSFS, "PHYSFSError"}, {SDL, "SDLError"}};

RbData::RbData() {
  for (size_t i = 0; i < ARRAY_SIZE(customExc); ++i)
    exc[customExc[i].id] = rb_define_class(customExc[i].name, rb_eException);

  exc[RGSS] = rb_define_class("RGSSError", rb_eStandardError);
  exc[Reset] =
      rb_define_class(rgssVer >= 3 ? "RGSSReset" : "Reset", rb_eException);

  exc[ErrnoENOENT] = rb_const_get(rb_const_get(rb_cObject, rb_intern("Errno")),
                                  rb_intern("ENOENT"));
  exc[IOError] = rb_eIOError;
  exc[TypeError] = rb_eTypeError;
  exc[ArgumentError] = rb_eArgError;
  exc[SystemExit] = rb_eSystemExit;
  exc[RuntimeError] = rb_eRuntimeError;
}

RbData::~RbData() {}

/* Indexed with Exception::Type */
const RbException excToRbExc[] = {
    RGSS,        /* RGSSError          */
    Reset,       /* Reset/RGSSReset */
    ErrnoENOENT, /* NoFileError        */
    IOError,

    TypeError,   ArgumentError, SystemExit, RuntimeError,

    PHYSFS, /* PHYSFSError */
    SDL,    /* SDLError    */
    MKXP    /* MKXPError   */
};

void raiseRbExc(Exception *exc) {
  VALUE str = rb_str_new2(exc->msg.c_str());
  RbData *data = getRbData();
  VALUE excClass = data->exc[excToRbExc[exc->type]];

  delete exc;

  rb_exc_raise(rb_class_new_instance(1, &str, excClass));
}

void raiseDisposedAccess(VALUE self) {
#if RAPI_FULL > 187
  const char *klassName = RTYPEDDATA_TYPE(self)->wrap_struct_name;
#else
  const char *klassName = rb_obj_classname(self);
#endif
  char buf[32];

  strncpy(buf, klassName, sizeof(buf));
  buf[0] = tolower(buf[0]);

  rb_raise(getRbData()->exc[RGSS], "disposed %s", buf);
}

int rb_get_args(int argc, VALUE *argv, const char *format, ...) {
  Exception *exc = 0;
  try{
    char c;
    VALUE *arg = argv;
    va_list ap;
    bool opt = false;
    int argI = 0;

    va_start(ap, format);

    while ((c = *format++)) {
      switch (c) {
      case '|':
        break;
      default:
        // FIXME print num of needed args vs provided
        if (argc <= argI && !opt)
          rb_raise(rb_eArgError, "wrong number of arguments");

        break;
      }

      if (argI >= argc)
        break;

      switch (c) {
      case 'o': {
        if (argI >= argc)
          break;

        VALUE *obj = va_arg(ap, VALUE *);

        *obj = *arg++;
        ++argI;

        break;
      }

      case 'S': {
        if (argI >= argc)
          break;

        VALUE *str = va_arg(ap, VALUE *);
        VALUE tmp = *arg;

        if (!RB_TYPE_P(tmp, RUBY_T_STRING))
          rb_raise(rb_eTypeError, "Argument %d: Expected string", argI);

        *str = tmp;
        ++argI;

        break;
      }

      case 's': {
        if (argI >= argc)
          break;

        const char **s = va_arg(ap, const char **);
        int *len = va_arg(ap, int *);

        VALUE tmp = *arg;

        if (!RB_TYPE_P(tmp, RUBY_T_STRING))
          rb_raise(rb_eTypeError, "Argument %d: Expected string", argI);

        *s = RSTRING_PTR(tmp);
        *len = RSTRING_LEN(tmp);
        ++argI;

        break;
      }

      case 'z': {
        if (argI >= argc)
          break;

        const char **s = va_arg(ap, const char **);

        VALUE tmp = *arg++;

        if (!RB_TYPE_P(tmp, RUBY_T_STRING))
          rb_raise(rb_eTypeError, "Argument %d: Expected string", argI);

        *s = RSTRING_PTR(tmp);
        ++argI;

        break;
      }

      case 'f': {
        if (argI >= argc)
          break;

        double *f = va_arg(ap, double *);
        VALUE fVal = *arg++;

        rb_float_arg(fVal, f, argI);

        ++argI;
        break;
      }

      case 'i': {
        if (argI >= argc)
          break;

        int *i = va_arg(ap, int *);
        VALUE iVal = *arg++;

        rb_int_arg(iVal, i, argI);

        ++argI;
        break;
      }

      case 'b': {
        if (argI >= argc)
          break;

        bool *b = va_arg(ap, bool *);
        VALUE bVal = *arg++;

        rb_bool_arg(bVal, b, argI);

        ++argI;
        break;
      }

      case 'n': {
        if (argI >= argc)
          break;

        ID *sym = va_arg(ap, ID *);

        VALUE symVal = *arg++;

        if (!SYMBOL_P(symVal))
          rb_raise(rb_eTypeError, "Argument %d: Expected symbol", argI);

        *sym = SYM2ID(symVal);
        ++argI;

        break;
      }

      case '|':
        opt = true;
        break;

      default:
        rb_raise(rb_eFatal, "invalid argument specifier %c", c);
      }
    }

#ifndef NDEBUG

    /* Pop remaining arg pointers off
     * the stack to check for RB_ARG_END */
    format--;

    while ((c = *format++)) {
      switch (c) {
      case 'o':
      case 'S':
        va_arg(ap, VALUE *);
        break;

      case 's':
        va_arg(ap, const char **);
        va_arg(ap, int *);
        break;

      case 'z':
        va_arg(ap, const char **);
        break;

      case 'f':
        va_arg(ap, double *);
        break;

      case 'i':
        va_arg(ap, int *);
        break;

      case 'b':
        va_arg(ap, bool *);
        break;
      }
    }

    // FIXME print num of needed args vs provided
    if (!c && argc > argI)
      rb_raise(rb_eArgError, "wrong number of arguments");

    /* Verify correct termination */
    void *argEnd = va_arg(ap, void *);
    (void)argEnd;
    assert(argEnd == RB_ARG_END_VAL);

#endif

    va_end(ap);

    return argI;
  } catch (const Exception &e) {
    exc = new Exception(e);
  }

  /* This should always be true if we reach here */
  if (exc) {
    /* Raising here is probably fine, right?
     * If any methods allocate something with a destructor before
     * calling this then they can probably be fixed to not do that. */
    raiseRbExc(exc);
  }

  return 0;
}

#if RAPI_MAJOR >= 2
#include <ruby/thread.h>

typedef struct gvl_guard_args {
	Exception *exc;
	void *(*func)(void *);
	void *args;
} gvl_guard_args;

static void *gvl_guard(void *args) {
	gvl_guard_args *gvl_args = (gvl_guard_args*)args;
	try{
		return gvl_args->func(gvl_args->args);
	} catch (const Exception &e) {
		gvl_args->exc = new Exception(e);
	} catch (const std::bad_alloc &) {
		/* Must not unwind through Ruby's C frames: that terminates. */
		gvl_args->exc = new Exception(Exception::MKXPError, MKXPZ_NATIVE_OOM_MESSAGE);
	}
	return 0;
}

void *drop_gvl_guard(void *(*func)(void *), void *args,
                            rb_unblock_function_t *ubf, void *data2) {
	gvl_guard_args gvl_args = {0, func, args};
	
	void *ret = rb_thread_call_without_gvl(&gvl_guard, &gvl_args, ubf, data2);
	
	Exception *&exc = gvl_args.exc;
	if (exc){
		Exception e(*exc);
		delete exc;
		throw e;
	}
	return ret;
}

#endif
