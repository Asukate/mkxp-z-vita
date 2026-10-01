#ifndef MKXPZ_VITA_RUBY_COMPAT_H
#define MKXPZ_VITA_RUBY_COMPAT_H

/* The Vita SDK exposes dev_t as a short, but Ruby 3.1's public arithmetic
 * headers do not provide SHORT2NUM.  Keep the value lossless after integer
 * promotion while avoiding a wider ABI assumption in the SDK headers. */
#ifndef SHORT2NUM
#define SHORT2NUM(value) INT2NUM(value)
#endif

#ifndef MAP_ANON
#define MAP_ANON 0x1000
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#ifndef PROT_NONE
#define PROT_NONE 0
#endif

/* Ruby's pthread backend uses its POSIX signal wrapper for a timer signal,
 * but the Vita libc only exposes the classic signal() interface. */
#ifndef POSIX_SIGNAL
#define posix_signal signal
#endif

#endif
