#ifndef _ENDIAN_H_
#define _ENDIAN_H_
#include <sys/cdefs.h>
#include <sys/_types.h>
#include <machine/endian.h>
#include <machine/_endian.h>
#define LITTLE_ENDIAN _LITTLE_ENDIAN
#define BIG_ENDIAN _BIG_ENDIAN
#define PDP_ENDIAN _PDP_ENDIAN
#define BYTE_ORDER _BYTE_ORDER
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#define __BYTE_ORDER BYTE_ORDER
static __inline__ unsigned short __bswap16(unsigned short _x) { return __builtin_bswap16(_x); }
static __inline__ unsigned int __bswap32(unsigned int _x) { return __builtin_bswap32(_x); }
#endif
