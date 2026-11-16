/* tgmath.h - type-generic math macros for CCCC */

#ifndef __TGMATH_H
#define __TGMATH_H

#include "math.h"
#include "complex.h"

#define __cccc_tg_unary(x, fd, ff, fl)                                         \
    _Generic((x), long double: fl, float: ff, default: fd)(x)

#define __cccc_tg_binary(x, y, fd, ff, fl)                                     \
    _Generic(((x) + (y)), long double: fl, float: ff, default: fd)(x, y)

#define __cccc_tg_unary_c(x, fd, ff, fl, cd, cf, cl)                           \
    _Generic((x),                                                              \
        long double _Complex: cl,                                              \
        double _Complex: cd,                                                   \
        float _Complex: cf,                                                    \
        long double: fl,                                                       \
        float: ff,                                                             \
        default: fd)(x)

#define __cccc_tg_binary_c(x, y, fd, ff, fl, cd, cf, cl)                       \
    _Generic(((x) + (y)),                                                      \
        long double _Complex: cl,                                              \
        double _Complex: cd,                                                   \
        float _Complex: cf,                                                    \
        long double: fl,                                                       \
        float: ff,                                                             \
        default: fd)(x, y)

#define __cccc_tg_real_or_complex_abs(x)                                       \
    _Generic((x),                                                              \
        long double _Complex: cabsl(x),                                        \
        double _Complex: cabs(x),                                              \
        float _Complex: cabsf(x),                                              \
        long double: fabsl(x),                                                 \
        float: fabsf(x),                                                       \
        default: fabs(x))

#define acos(x)                                                                \
    __cccc_tg_unary_c((x), acos, acosf, acosl, cacos, cacosf, cacosl)
#define asin(x)                                                                \
    __cccc_tg_unary_c((x), asin, asinf, asinl, casin, casinf, casinl)
#define atan(x)                                                                \
    __cccc_tg_unary_c((x), atan, atanf, atanl, catan, catanf, catanl)
#define cos(x) __cccc_tg_unary_c((x), cos, cosf, cosl, ccos, ccosf, ccosl)
#define sin(x) __cccc_tg_unary_c((x), sin, sinf, sinl, csin, csinf, csinl)
#define tan(x) __cccc_tg_unary_c((x), tan, tanf, tanl, ctan, ctanf, ctanl)
#define exp(x) __cccc_tg_unary_c((x), exp, expf, expl, cexp, cexpf, cexpl)
#define log(x) __cccc_tg_unary_c((x), log, logf, logl, clog, clogf, clogl)
#define sqrt(x)                                                                \
    __cccc_tg_unary_c((x), sqrt, sqrtf, sqrtl, csqrt, csqrtf, csqrtl)
#define acosh(x)                                                               \
    __cccc_tg_unary_c((x), acosh, acoshf, acoshl, cacosh, cacoshf, cacoshl)
#define asinh(x)                                                               \
    __cccc_tg_unary_c((x), asinh, asinhf, asinhl, casinh, casinhf, casinhl)
#define atanh(x)                                                               \
    __cccc_tg_unary_c((x), atanh, atanhf, atanhl, catanh, catanhf, catanhl)
#define cosh(x)                                                                \
    __cccc_tg_unary_c((x), cosh, coshf, coshl, ccosh, ccoshf, ccoshl)
#define sinh(x)                                                                \
    __cccc_tg_unary_c((x), sinh, sinhf, sinhl, csinh, csinhf, csinhl)
#define tanh(x)                                                                \
    __cccc_tg_unary_c((x), tanh, tanhf, tanhl, ctanh, ctanhf, ctanhl)
#define fabs(x)     __cccc_tg_real_or_complex_abs((x))
#define floor(x)    __cccc_tg_unary((x), floor, floorf, floorl)
#define ceil(x)     __cccc_tg_unary((x), ceil, ceilf, ceill)
#define trunc(x)    __cccc_tg_unary((x), trunc, truncf, truncl)
#define round(x)    __cccc_tg_unary((x), round, roundf, roundl)
#define pow(x, y)                                                              \
    __cccc_tg_binary_c((x), (y), pow, powf, powl, cpow, cpowf, cpowl)
#define fmod(x, y)  __cccc_tg_binary((x), (y), fmod, fmodf, fmodl)
#define atan2(x, y) __cccc_tg_binary((x), (y), atan2, atan2f, atan2l)
#define hypot(x, y) __cccc_tg_binary((x), (y), hypot, hypotf, hypotl)

#endif /* __TGMATH_H */
