// -c=native C23 fromfp/ufromfp/fromfpx/ufromfpx family shims (#1195):
// self-contained ports of src/stdlib/math.c, immune to the glibc 2.43
// fromfp ABI signature split.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating and rationale live in the
// matching serialize_*_shims() in src/serialize_shims.c.

// >>> shim: impl
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
// <<< shim

// >>> shim: fromfp
static intmax_t __cccc_native_fromfp(double x, int rnd,
        unsigned int width) {
    return (intmax_t)__cccc_fromfp_impl(x, rnd, width, 0, 0);
}
// <<< shim

// >>> shim: ufromfp
static uintmax_t __cccc_native_ufromfp(double x, int rnd,
        unsigned int width) {
    return (uintmax_t)__cccc_fromfp_impl(x, rnd, width, 1, 0);
}
// <<< shim

// >>> shim: fromfpx
static intmax_t __cccc_native_fromfpx(double x, int rnd,
        unsigned int width) {
    return (intmax_t)__cccc_fromfp_impl(x, rnd, width, 0, 1);
}
// <<< shim

// >>> shim: ufromfpx
static uintmax_t __cccc_native_ufromfpx(double x, int rnd,
        unsigned int width) {
    return (uintmax_t)__cccc_fromfp_impl(x, rnd, width, 1, 1);
}
// <<< shim

// >>> shim: fromfpf
static intmax_t __cccc_native_fromfpf(float x, int rnd,
        unsigned int width) {
    return (intmax_t)__cccc_fromfp_impl((double)x, rnd, width, 0, 0);
}
// <<< shim

// >>> shim: ufromfpf
static uintmax_t __cccc_native_ufromfpf(float x, int rnd,
        unsigned int width) {
    return (uintmax_t)__cccc_fromfp_impl((double)x, rnd, width, 1, 0);
}
// <<< shim

// >>> shim: fromfpxf
static intmax_t __cccc_native_fromfpxf(float x, int rnd,
        unsigned int width) {
    return (intmax_t)__cccc_fromfp_impl((double)x, rnd, width, 0, 1);
}
// <<< shim

// >>> shim: ufromfpxf
static uintmax_t __cccc_native_ufromfpxf(float x, int rnd,
        unsigned int width) {
    return (uintmax_t)__cccc_fromfp_impl((double)x, rnd, width, 1, 1);
}
// <<< shim

// >>> shim: fromfpl
static intmax_t __cccc_native_fromfpl(long double x, int rnd,
        unsigned int width) {
    return (intmax_t)__cccc_fromfp_impl((double)x, rnd, width, 0, 0);
}
// <<< shim

// >>> shim: ufromfpl
static uintmax_t __cccc_native_ufromfpl(long double x, int rnd,
        unsigned int width) {
    return (uintmax_t)__cccc_fromfp_impl((double)x, rnd, width, 1, 0);
}
// <<< shim

// >>> shim: fromfpxl
static intmax_t __cccc_native_fromfpxl(long double x, int rnd,
        unsigned int width) {
    return (intmax_t)__cccc_fromfp_impl((double)x, rnd, width, 0, 1);
}
// <<< shim

// >>> shim: ufromfpxl
static uintmax_t __cccc_native_ufromfpxl(long double x, int rnd,
        unsigned int width) {
    return (uintmax_t)__cccc_fromfp_impl((double)x, rnd, width, 1, 1);
}
// <<< shim
