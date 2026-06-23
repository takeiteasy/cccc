/* stdint.h - fixed-width integer types for CCCC C compiler */

#ifndef __STDINT_H
#define __STDINT_H

/* On this VM, long is 8 bytes */
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

typedef long intptr_t;
typedef unsigned long uintptr_t;

typedef long intmax_t;
typedef unsigned long uintmax_t;

#define INTPTR_MIN  (-9223372036854775807L-1)
#define INTPTR_MAX  9223372036854775807L
#define UINTPTR_MAX 18446744073709551615UL

#define INTMAX_MIN  (-9223372036854775807L-1)
#define INTMAX_MAX  9223372036854775807L
#define UINTMAX_MAX 18446744073709551615UL

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define INT32_MIN  (-2147483648)
#define INT32_MAX  2147483647
#define INT64_MIN  (-9223372036854775807L-1)
#define INT64_MAX  9223372036854775807L

#define UINT8_MAX  255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U
#define UINT64_MAX 18446744073709551615UL

/* 7.18.4 Macros for integer constants.
 * Append the suffix that gives a constant of at least the requested width.
 * On the CCCC target, long long / unsigned long long are 64-bit, so the
 * 64-bit and maximum-width forms use the LL / ULL suffixes. */
#define INT8_C(x)    x
#define INT16_C(x)   x
#define INT32_C(x)   x
#define INT64_C(x)   x ## LL
#define UINT8_C(x)   x
#define UINT16_C(x)  x
#define UINT32_C(x)  x ## U
#define UINT64_C(x)  x ## ULL
#define INTMAX_C(x)  x ## LL
#define UINTMAX_C(x) x ## ULL

#endif /* __STDINT_H */
