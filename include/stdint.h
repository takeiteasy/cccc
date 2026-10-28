/* stdint.h - fixed-width integer types for CCCC C compiler */

#ifndef __STDINT_H
#define __STDINT_H

/* Under CCCC the types are fixed here; a real host compiler hands off to the */
/* host's own <stdint.h> (below). */
#ifdef __CCCC__

/* On this VM, long is 8 bytes */
typedef signed char    int8_t;
typedef short          int16_t;
typedef int            int32_t;
#ifdef __APPLE__
/* The macOS SDK spells int64_t `long long`; both are 8 bytes, but the spelling
 * must match so a struct reached through the SDK and through here is one type.
 */
typedef long long int64_t;
#else
typedef long           int64_t;
#endif

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
#ifdef __APPLE__
typedef unsigned long long uint64_t;
#else
typedef unsigned long  uint64_t;
#endif

typedef long           intptr_t;
typedef unsigned long  uintptr_t;

typedef long           intmax_t;
typedef unsigned long  uintmax_t;

#define INTPTR_MIN  (-9223372036854775807L - 1)
#define INTPTR_MAX  9223372036854775807L
#define UINTPTR_MAX 18446744073709551615UL

#define INTMAX_MIN  (-9223372036854775807L - 1)
#define INTMAX_MAX  9223372036854775807L
#define UINTMAX_MAX 18446744073709551615UL

#define INT8_MIN    (-128)
#define INT8_MAX    127
#define INT16_MIN   (-32768)
#define INT16_MAX   32767
#define INT32_MIN   (-2147483648)
#define INT32_MAX   2147483647
#ifdef __APPLE__
#define INT64_MIN (-9223372036854775807LL - 1)
#define INT64_MAX 9223372036854775807LL
#else
#define INT64_MIN   (-9223372036854775807L - 1)
#define INT64_MAX   9223372036854775807L
#endif

#define UINT8_MAX   255
#define UINT16_MAX  65535
#define UINT32_MAX  4294967295U
#ifdef __APPLE__
#define UINT64_MAX 18446744073709551615ULL
#else
#define UINT64_MAX  18446744073709551615UL
#endif

/* 7.18.3 Limits of other integer types. size_t/ptrdiff_t are fixed typedefs
 * (include/stddef.h: `unsigned long`/`long` on every CCCC target), so these
 * two are unconditional -- unlike wchar_t/wint_t below, there is no
 * per-platform width to track. #1282: SIZE_MAX is used by cccc's own
 * src/stdlib/stdlib.c and src/ops.c (overflow checks ahead of a malloc), a
 * bundled-header gap the self-hosting spike hit; PTRDIFF_MIN/MAX are added
 * alongside as the obvious sibling (same #1275/#1277/#1279/#1282 pattern of
 * one round finding a symbol and the next finding its neighbour). */
#define PTRDIFF_MIN (-9223372036854775807L - 1)
#define PTRDIFF_MAX 9223372036854775807L
#define SIZE_MAX    18446744073709551615UL

/* sig_atomic_t is `int` (include/signal.h) on every CCCC target. */
#define SIG_ATOMIC_MIN (-2147483647 - 1)
#define SIG_ATOMIC_MAX 2147483647

/* wchar_t's signedness is a real per-platform ABI split (include/stddef.h's
 * own comment: unsigned on Linux aarch64, signed everywhere else CCCC
 * supports) -- mirror that split here rather than hardcoding one sign.
 * wint_t (include/wchar.h) is unconditionally `unsigned int` on every CCCC
 * target, so WINT_MIN/MAX need no split. */
#if defined(__linux__) && defined(__aarch64__)
#define WCHAR_MIN 0
#define WCHAR_MAX 4294967295U
#else
#define WCHAR_MIN (-2147483647 - 1)
#define WCHAR_MAX 2147483647
#endif
#define WINT_MIN 0U
#define WINT_MAX 4294967295U

/* 7.18.4 Macros for integer constants.
 * Append the suffix that gives a constant of at least the requested width.
 * On the CCCC target, long long / unsigned long long are 64-bit, so the
 * 64-bit and maximum-width forms use the LL / ULL suffixes. */
#define INT8_C(x)    x
#define INT16_C(x)   x
#define INT32_C(x)   x
#define INT64_C(x)   x##LL
#define UINT8_C(x)   x
#define UINT16_C(x)  x
#define UINT32_C(x)  x##U
#define UINT64_C(x)  x##ULL
#define INTMAX_C(x)  x##LL
#define UINTMAX_C(x) x##ULL

#else
#include_next <stdint.h>
#endif /* __CCCC__ */

#endif /* __STDINT_H */
