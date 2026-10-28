/* inttypes.h - format macros for integer types */

#ifndef __INTTYPES_H
#define __INTTYPES_H

#include <stdint.h> /* #1070: angle-bracket for a correct #include_next hand-off under real GCC */

#define PRId8  "d"
#define PRIi8  "i"
#define PRId16 "d"
#define PRIi16 "i"
#define PRId32 "d"
#define PRIi32 "i"
#ifdef __APPLE__
#define PRId64 "lld"
#define PRIi64 "lld"
#define PRIu64 "llu"
#else
#define PRId64 "ld"
#define PRIi64 "ld"
#define PRIu64 "lu"
#endif

#endif /* __INTTYPES_H */
