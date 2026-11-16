/*
** fs-natives.cpp
**
** HardRPGFs.stat_cache_stats: counters of the Vita stat cache (src/vita_stat_cache.c), for
** probes and benchmarks. nil elsewhere.
*/

#include <ruby.h>

#ifdef __vita__
extern "C" int vitaStatCacheStats(char *buf, int size);
#endif

static VALUE fsStatCacheStats(VALUE)
{
#ifdef __vita__
    char buf[512];
    vitaStatCacheStats(buf, sizeof(buf));
    return rb_str_new_cstr(buf);
#else
    return Qnil;
#endif
}

extern "C" void fsNativesInit()
{
    VALUE m = rb_define_module("HardRPGFs");
    rb_define_singleton_method(m, "stat_cache_stats", RUBY_METHOD_FUNC(fsStatCacheStats), 0);
}
