/* Exercise the shipped coroutine backend with real pthreads and injected
 * creation failures. No Ruby VM or mock scheduler substitutes this seam. */
#include "Context.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fault, outstanding_attrs;
int __real_pthread_attr_init(pthread_attr_t *);
int __real_pthread_attr_destroy(pthread_attr_t *);
int __real_pthread_attr_setstack(pthread_attr_t *, void *, size_t);
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);

int __wrap_pthread_attr_init(pthread_attr_t *attr) {
    if (fault == 1) { fault = 0; errno = 0; return ENOMEM; }
    int result = __real_pthread_attr_init(attr);
    if (!result) ++outstanding_attrs;
    return result;
}
int __wrap_pthread_attr_destroy(pthread_attr_t *attr) {
    int result = __real_pthread_attr_destroy(attr);
    if (!result) --outstanding_attrs;
    return result;
}
int __wrap_pthread_attr_setstack(pthread_attr_t *attr, void *stack, size_t size) {
    if (fault == 2) { fault = 0; errno = 0; return EINVAL; }
    return __real_pthread_attr_setstack(attr, stack, size);
}
int __wrap_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    if (fault == 3) { fault = 0; errno = 0; return ENOMEM; }
    return __real_pthread_cond_init(cond, attr);
}
int __wrap_pthread_create(pthread_t *id, const pthread_attr_t *attr,
                          void *(*entry)(void *), void *argument) {
    if (fault == 4) { fault = 0; errno = 0; return EAGAIN; }
    return __real_pthread_create(id, attr, entry, argument);
}

static void child(struct coroutine_context *from, struct coroutine_context *self) {
    coroutine_transfer(self, from);
    abort();
}

int main(int argc, char **argv) {
    int mode = argc > 1 ? atoi(argv[1]) : 0;
    struct coroutine_context main_context = {0}, context = {0};
    void *stack = NULL;
    if (posix_memalign(&stack, 4096, 512 * 1024)) return 2;
    coroutine_initialize_main(&main_context);
    coroutine_initialize(&context, child, stack, 512 * 1024);
    if (mode) {
        fault = mode;
        struct coroutine_context *result = coroutine_transfer(&main_context, &context);
        int error = errno;
        int lock_result = pthread_mutex_trylock(&main_context.shared->guard);
        if (!lock_result) pthread_mutex_unlock(&main_context.shared->guard);
        int expected = mode == 2 ? EINVAL : mode == 4 ? EAGAIN : ENOMEM;
        int valid = !result && error == expected && !lock_result &&
                    !context.shared && !context.from && !outstanding_attrs;
        printf("failure=%d errno=%d expected=%d guard=%d attrs=%d restored=%d\n",
               mode, error, expected, lock_result, outstanding_attrs,
               !context.shared && !context.from);
        if (!valid) return 1;
        /* A failed first transfer must leave the scheduler usable. */
        if (coroutine_transfer(&main_context, &context) != &context) return 3;
        coroutine_destroy(&context);
        coroutine_destroy(&context);
    } else {
        for (int i = 0; i < 2000; ++i) {
            coroutine_initialize(&context, child, stack, 512 * 1024);
            if (coroutine_transfer(&main_context, &context) != &context) return 3;
            coroutine_destroy(&context);
            coroutine_destroy(&context);
            if (main_context.shared->count != 1) return 4;
        }
        printf("completed=2000 shared_count=%zu attrs=%d\n",
               main_context.shared->count, outstanding_attrs);
    }
    coroutine_destroy(&main_context);
    free(stack);
    return outstanding_attrs != 0;
}
