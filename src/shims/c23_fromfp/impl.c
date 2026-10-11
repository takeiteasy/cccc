#include <fenv.h>
#include <stdint.h>
static double __cccc_fromfp_round(double x, int rnd) {
    switch (rnd) {
        case 0: return ceil(x);
        case 1: return floor(x);
        case 2: return trunc(x);
        case 3: return round(x);
        default: return rint(x);
    }
}
static long long __cccc_fromfp_impl(double x, int rnd,
        unsigned int width, int is_unsigned, int extended) {
    if (__builtin_isnan(x) || __builtin_isinf(x) || width == 0) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    double r = __cccc_fromfp_round(x, rnd);
    double lo, hi;
    if (is_unsigned) {
        lo = 0.0;
        hi = (width >= 64) ? 18446744073709551615.0
                          : (ldexp(1.0, (int)width) - 1.0);
    } else {
        hi = (width >= 64) ? 9223372036854775807.0
                          : (ldexp(1.0, (int)width - 1) - 1.0);
        lo = (width >= 64) ? -9223372036854775808.0
                          : -ldexp(1.0, (int)width - 1);
    }
    if (r < lo || r > hi) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    if (extended && r != x)
        feraiseexcept(FE_INEXACT);
    return is_unsigned ? (long long)(uint64_t)r
                      : (long long)(int64_t)r;
}
