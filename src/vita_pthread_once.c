/* Newlib declares pthread_once, but the Vita pthread library omits it.
 * libstdc++ uses it to initialize the mutex protecting local static objects.
 */
#include <pthread.h>
#include <psp2/kernel/threadmgr.h>

int pthread_once(pthread_once_t *control, void (*initialize)(void))
{
    if (__atomic_load_n(&control->done, __ATOMIC_ACQUIRE))
        return 0;

    int expected = 0;
    if (__atomic_compare_exchange_n(&control->state, &expected, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        initialize();
        __atomic_store_n(&control->done, 1, __ATOMIC_RELEASE);
    } else {
        while (!__atomic_load_n(&control->done, __ATOMIC_ACQUIRE))
            sceKernelDelayThread(1000);
    }
    return 0;
}
