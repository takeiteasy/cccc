/* tgmath.h - type-generic math macros for JCC */

#ifndef __TGMATH_H
#define __TGMATH_H

#include "math.h"
#include "complex.h"

#define __jcc_tg_unary(x, fd, ff, fl) _Generic((x), \
    long double: fl, \
    float: ff, \
    default: fd)(x)

#define __jcc_tg_binary(x, y, fd, ff, fl) _Generic(((x) + (y)), \
    long double: fl, \
    float: ff, \
    default: fd)(x, y)

#define __jcc_tg_real_or_complex_abs(x) _Generic((x), \
    long double _Complex: cabsl(x), \
    double _Complex: cabs(x), \
    float _Complex: cabsf(x), \
    long double: fabsl(x), \
    float: fabsf(x), \
    default: fabs(x))

#define acos(x) __jcc_tg_unary((x), acos, acosf, acosl)
#define asin(x) __jcc_tg_unary((x), asin, asinf, asinl)
#define atan(x) __jcc_tg_unary((x), atan, atanf, atanl)
#define cos(x) __jcc_tg_unary((x), cos, cosf, cosl)
#define sin(x) __jcc_tg_unary((x), sin, sinf, sinl)
#define tan(x) __jcc_tg_unary((x), tan, tanf, tanl)
#define exp(x) __jcc_tg_unary((x), exp, expf, expl)
#define log(x) __jcc_tg_unary((x), log, logf, logl)
#define sqrt(x) __jcc_tg_unary((x), sqrt, sqrtf, sqrtl)
#define fabs(x) __jcc_tg_real_or_complex_abs((x))
#define floor(x) __jcc_tg_unary((x), floor, floorf, floorl)
#define ceil(x) __jcc_tg_unary((x), ceil, ceilf, ceill)
#define trunc(x) __jcc_tg_unary((x), trunc, truncf, truncl)
#define round(x) __jcc_tg_unary((x), round, roundf, roundl)
#define pow(x, y) __jcc_tg_binary((x), (y), pow, powf, powl)
#define fmod(x, y) __jcc_tg_binary((x), (y), fmod, fmodf, fmodl)
#define atan2(x, y) __jcc_tg_binary((x), (y), atan2, atan2f, atan2l)
#define hypot(x, y) __jcc_tg_binary((x), (y), hypot, hypotf, hypotl)

#endif /* __TGMATH_H */
