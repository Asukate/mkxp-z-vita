#!/usr/bin/env python3
"""Exercise Ruby's localtime path with Vita's split RTC/newlib timezone APIs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent


def block(source, start):
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


prefix = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <time.h>
#define HAVE_GMTIME_R 1
#define HAVE_MKTIME 1
#define LOCALTIME_OVERFLOW_PROBLEM 1
#if TEST_VITA
#define __vita__ 1
#endif
static int offset;
static void update_tz(void) {}
/* Vita localtime_r applies the system RTC offset; newlib mktime instead
 * uses its own TZ state. Emulate that mismatch without changing host TZ. */
static struct tm *rtc_localtime(const time_t *t, struct tm *out) {
    if (*t < 0) return NULL;
    time_t local = *t + offset;
    return gmtime_r(&local, out);
}
#define localtime_r rtc_localtime
#define mktime timegm
typedef uintptr_t VALUE;
static const VALUE str_utc = 0x1000;
#define LONG2FIX(n) ((VALUE)(n) * 2 + 1)
static struct tm *localtime_with_gmtoff_zone(const time_t *t, struct tm *tm,
                                           long *gmtoff, VALUE *zone) {
    (void)t; (void)tm; (void)gmtoff; (void)zone;
    return NULL; /* Failing conversion must not leave a garbage Ruby VALUE. */
}
static VALUE rb_fstring(VALUE value) { assert(value == str_utc); return value; }
static void rb_gc_register_mark_object(VALUE value) { assert(value == str_utc); }
'''

with tempfile.TemporaryDirectory(prefix='ruby-localtime-') as directory:
    tmp = Path(directory)
    subprocess.run(['tar', '--use-compress-program=zstd', '-xf',
                    str(root / 'deps/vendor/ruby-vita-a2d396e-tree.tar.zst'),
                    '-C', str(tmp), 'time.c'], check=True)
    subprocess.run(['git', 'apply', '--include=time.c',
                    str(root / 'deps/ruby-vita-a2d396e-clean.patch')], cwd=tmp, check=True)
    source = (tmp / 'time.c').read_text()
    function = block(source, source.index('static struct tm *\nrb_localtime_r('))
    fallback = block(source, source.index('    {\n        /* Use the current time offset'))
    test = prefix + function + '\nstatic VALUE fallback(VALUE *zone_ret, int *isdst_ret) {\n'
    test += 'struct tm tm;\n' + fallback + '\n}\n'
    test += r'''
int main(void) {
    const time_t stamps[] = {1700000000, 1793759093, 2000000000};
    const int offsets[] = {0, -10800, 19800, 3600};
    for (unsigned i = 0; i < sizeof(stamps)/sizeof(*stamps); ++i) {
        for (unsigned j = 0; j < sizeof(offsets)/sizeof(*offsets); ++j) {
            offset = offsets[j]; struct tm result;
            struct tm *got = rb_localtime_r(&stamps[i], &result);
            assert((got != NULL) == (TEST_VITA || offset == 0));
            if (got) {
                time_t local = stamps[i] + offset; struct tm expected;
                gmtime_r(&local, &expected);
                assert(result.tm_hour == expected.tm_hour);
                assert(result.tm_mday == expected.tm_mday);
            }
        }
    }
    time_t invalid = -1; struct tm tm;
    assert(rb_localtime_r(&invalid, &tm) == NULL);
    VALUE zone = 0; int isdst = -1;
    assert(fallback(&zone, &isdst) == LONG2FIX(0) && zone == str_utc);
    assert(isdst == 0);
    return 0;
}
'''
    file = tmp / 'localtime.c'
    file.write_text(test)
    for vita in (1, 0):
        exe = tmp / f'localtime-{vita}'
        subprocess.run(['cc', '-std=c99', '-O1', '-ftrivial-auto-var-init=pattern',
                        f'-DTEST_VITA={vita}', str(file), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], cwd=tmp, check=True)
print('Ruby localtime RTC offsets, host overflow check and failed fallback passed')
