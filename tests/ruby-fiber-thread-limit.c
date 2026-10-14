/* Bound live pthreads in a host Ruby test to model a finite native-thread pool. */
#define _GNU_SOURCE
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>

static _Atomic int live, peak, created, joined, failures;

int pthread_create(pthread_t *id, const pthread_attr_t *attr,
                   void *(*entry)(void *), void *argument) {
    static int (*real)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    if (!real) real = dlsym(RTLD_NEXT, "pthread_create");
    int count = atomic_fetch_add(&live, 1) + 1;
    if (count > 32) {
        atomic_fetch_sub(&live, 1);
        ++failures;
        errno = 0;
        return EAGAIN;
    }
    if (count > peak) peak = count;
    int result = real(id, attr, entry, argument);
    if (result) atomic_fetch_sub(&live, 1);
    else ++created;
    return result;
}

int pthread_join(pthread_t id, void **result) {
    static int (*real)(pthread_t, void **);
    if (!real) real = dlsym(RTLD_NEXT, "pthread_join");
    int value = real(id, result);
    if (!value) { atomic_fetch_sub(&live, 1); ++joined; }
    return value;
}

__attribute__((destructor)) static void statistics(void) {
    fprintf(stderr, "fiber_thread_limit created=%d joined=%d live=%d peak=%d failures=%d\n",
            created, joined, live, peak, failures);
}
