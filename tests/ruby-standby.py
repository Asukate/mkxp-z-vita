#!/usr/bin/env python3
"""Apply the shipped Ruby patch and verify Vita scheduling selects supported APIs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent

def function(source, name):
    start = source.index(name)
    start = source.rfind('\n', 0, source.rfind('\n', 0, start)) + 1
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

prefix = r'''
#include <assert.h>
#include <stddef.h>
#define __vita__ 1
#define SIGVTALRM 26
#define HAVE_PTHREAD_CONDATTR_SETCLOCK 1
#define HAVE_CLOCK_GETTIME 1
#define CLOCK_REALTIME 1
#define CLOCK_MONOTONIC 4
typedef int pthread_condattr_t;
typedef int rb_hrtime_t;
typedef struct { int ractor; } rb_thread_t;
static int waits, blocked;
static void native_cond_sleep(rb_thread_t *th, rb_hrtime_t *rel) {
 (void)th; (void)rel; ++waits;
}
#define rb_ractor_blocking_threads_inc(...) (++blocked)
#define rb_ractor_blocking_threads_dec(...) (--blocked)
'''

def policy(source):
    timers = source[source.index('#if defined(SIGVTALRM)'):source.index('enum rtimer_state')]
    clocks = source[source.index('#if defined(HAVE_PTHREAD_CONDATTR_SETCLOCK)') if '#if !defined(__vita__) && defined(HAVE_PTHREAD_CONDATTR_SETCLOCK)' not in source else source.index('#if !defined(__vita__) && defined(HAVE_PTHREAD_CONDATTR_SETCLOCK)'):]
    clocks = clocks[:clocks.index('/* 100ms.')]
    return timers + clocks

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    subprocess.run(['tar', '--use-compress-program=zstd', '-xf', str(root / 'deps/vendor/ruby-vita-a2d396e-tree.tar.zst'), '-C', str(tmp)], check=True)
    path = tmp / 'thread_pthread.c'
    original = path.read_text()
    subprocess.run(['git', 'apply', '--check', str(root / 'deps/ruby-vita-a2d396e-clean.patch')], cwd=tmp, check=True)
    subprocess.run(['git', 'apply', str(root / 'deps/ruby-vita-a2d396e-clean.patch')], cwd=tmp, check=True)
    patched = path.read_text()
    # The original picks a signal timer and requests a clock pthread-embedded
    # ignores. The shipped patch must change both platform decisions.
    for name, source, expected in [('original', original, 1), ('patched', patched, 0)]:
        test = prefix + policy(source)
        if name == 'patched':
            test += function(source, 'native_sleep(rb_thread_t *th, rb_hrtime_t *rel)')
            test += function(source, 'rb_thread_create_timer_thread(void)')
            test += function(source, 'rb_sigwait_fd_get(const rb_thread_t *th)')
            test += '''
int main(void) {
 assert(UBF_TIMER==UBF_TIMER_NONE && condattr_monotonic==NULL);
 rb_thread_t th={0}; rb_hrtime_t rel=100;
 rb_thread_create_timer_thread(); assert(rb_sigwait_fd_get(&th)==-1);
 native_sleep(&th,&rel); native_sleep(&th,NULL); native_sleep(&th,&rel);
 assert(waits==3 && blocked==0); return 0;
}
'''
        else:
            test += 'int main(void) { return UBF_TIMER!=UBF_TIMER_NONE || condattr_monotonic!=NULL; }\n'
        file = tmp / (name + '.c'); file.write_text(test)
        exe = tmp / name
        subprocess.run(['cc', '-std=c99', str(file), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)])
        assert result.returncode == expected, (name, result.returncode)
print('Ruby patch application and Vita condition-wait scheduling regression passed')
