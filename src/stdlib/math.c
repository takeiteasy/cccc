// math.h stdlib function registration
#include "../cccc.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <fenv.h>

// nexttowardf(float, long double): CCCC promotes the long double arg to double
// (the VM's long double width), so we need a wrapper that accepts (float,
// double) and casts back to float before delegating to nextafterf (#498).
static float cccc_nexttowardf(float x, double y) {
    return nextafterf(x, (float)y);
}

// C23 functions shared with -c=native output (see c23_math/runtime.c). The
// VM passes pointers as long long, models long double as double and reads a
// full 64-bit return, so pointer-taking and int-returning entries need these
// adapters; the rest are registered directly.
#include "c23_math/runtime.c"

#define CCCC_C23_PTR2(NAME, T, U)                                              \
    static long long cccc_##NAME(long long x, long long y) {                   \
        return __cccc_native_##NAME((T *)(intptr_t)x, (U *)(intptr_t)y);       \
    }
CCCC_C23_PTR2(totalorder, const double, const double)
CCCC_C23_PTR2(totalorderf, const float, const float)
CCCC_C23_PTR2(totalordermag, const double, const double)
CCCC_C23_PTR2(totalordermagf, const float, const float)
CCCC_C23_PTR2(canonicalize, double, const double)
CCCC_C23_PTR2(canonicalizef, float, const float)
#undef CCCC_C23_PTR2

static double cccc_getpayload(long long x) {
    return __cccc_native_getpayload((const double *)(intptr_t)x);
}
static float cccc_getpayloadf(long long x) {
    return __cccc_native_getpayloadf((const float *)(intptr_t)x);
}

static long long cccc_setpayload(long long x, double payload) {
    return __cccc_native_setpayload((double *)(intptr_t)x, payload);
}
static long long cccc_setpayloadf(long long x, float payload) {
    return __cccc_native_setpayloadf((float *)(intptr_t)x, payload);
}
static long long cccc_setpayloadsig(long long x, double payload) {
    return __cccc_native_setpayloadsig((double *)(intptr_t)x, payload);
}
static long long cccc_setpayloadsigf(long long x, float payload) {
    return __cccc_native_setpayloadsigf((float *)(intptr_t)x, payload);
}

// ---- issignaling/iseqsig (backing __cccc_issignaling_*/__cccc_iseqsig_*
// macros dispatched from <math.h>) ----
// issignaling tests the quiet bit directly on the raw bit pattern rather
// than going through isnan()/arithmetic, since either would quiet a
// signaling NaN before it could be observed.
static long long cccc_issignaling_d(double x) {
    uint64_t u = __cccc_c23_d2b(x);
    return ((u & 0x7FF0000000000000ULL) ==
            0x7FF0000000000000ULL) &&               // NaN or Inf
           (u & __CCCC_C23_DBL_PAYLOAD_MAX) != 0 && // NaN, not Inf
           !(u & __CCCC_C23_DBL_QUIET_BIT);         // quiet bit clear
}
static long long cccc_issignaling_f(float x) {
    uint32_t u = __cccc_c23_f2b(x);
    return ((u & 0x7F800000U) == 0x7F800000U) &&
           (u & __CCCC_C23_FLT_PAYLOAD_MAX) != 0 &&
           !(u & __CCCC_C23_FLT_QUIET_BIT);
}

// iseqsig: an equality compare that raises FE_INVALID for a signaling NaN
// operand (unlike ==, which only traps quiet-vs-quiet). x != x below is
// used instead of isnan() so it composes with the signaling check without
// an extra function call.
static long long cccc_iseqsig_d(double x, double y) {
    if (cccc_issignaling_d(x) || cccc_issignaling_d(y))
        feraiseexcept(FE_INVALID);
    return x == y;
}
static long long cccc_iseqsig_f(float x, float y) {
    if (cccc_issignaling_f(x) || cccc_issignaling_f(y))
        feraiseexcept(FE_INVALID);
    return x == y;
}

// ==================== math.h classification macro fixes (#778) ==========
//
// isnan/isinf/isfinite/signbit/fpclassify/isnormal in include/math.h used
// to be hand-written formulas referencing a bare `isnan`/`isinf` that was
// never actually defined anywhere (only __builtin_isnan/__builtin_isinf
// existed), so isfinite/fpclassify/isnormal failed to compile at all, and
// signbit(x) ((x) < 0) was simply wrong for -0.0 and NaN. Backing these
// with real bit-pattern functions (mirroring the issignaling/iseqsig
// helpers above) fixes both: the compile failure and the semantics.

// Host-side mirrors of the FP_* classification constants declared in
// include/math.h (guest side) -- this file is compiled by the host
// toolchain, so it can't see the guest header's macros directly.
#define CCCC_FP_INFINITE  1
#define CCCC_FP_NAN       2
#define CCCC_FP_NORMAL    3
#define CCCC_FP_SUBNORMAL 4
#define CCCC_FP_ZERO      5

// NB: uses CCCC_D/FLT_MANT_MASK (the full mantissa, including the quiet
// bit) to test "is the mantissa nonzero", NOT CCCC_D/FLT_PAYLOAD_MAX (which
// deliberately excludes the quiet bit, for getpayload/setpayload). A plain
// quiet NaN with zero payload -- e.g. 0.0/0.0, whose mantissa is just the
// quiet bit itself -- has a nonzero full mantissa but a zero payload, so
// using PAYLOAD_MAX here would misclassify it as infinity.
static long long cccc_isnan_d(double x) {
    uint64_t u = __cccc_c23_d2b(x);
    return (u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL &&
           (u & __CCCC_C23_DBL_MANT_MASK) != 0;
}
static long long cccc_isnan_f(float x) {
    uint32_t u = __cccc_c23_f2b(x);
    return (u & 0x7F800000U) == 0x7F800000U &&
           (u & __CCCC_C23_FLT_MANT_MASK) != 0;
}

static long long cccc_isinf_d(double x) {
    uint64_t u = __cccc_c23_d2b(x);
    return (u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL &&
           (u & __CCCC_C23_DBL_MANT_MASK) == 0;
}
static long long cccc_isinf_f(float x) {
    uint32_t u = __cccc_c23_f2b(x);
    return (u & 0x7F800000U) == 0x7F800000U &&
           (u & __CCCC_C23_FLT_MANT_MASK) == 0;
}

// signbit tests the raw sign bit directly -- correct for -0.0 (bit set,
// unlike `x < 0`) and for NaN (whatever its actual sign bit is, unlike
// `x < 0`'s always-false result since every NaN comparison is false).
static long long cccc_signbit_d(double x) {
    return __cccc_c23_d2b(x) >> 63;
}
static long long cccc_signbit_f(float x) {
    return __cccc_c23_f2b(x) >> 31;
}

static long long cccc_fpclassify_d(double x) {
    uint64_t u   = __cccc_c23_d2b(x);
    uint64_t exp = u & 0x7FF0000000000000ULL;
    uint64_t mant =
        u &
        __CCCC_C23_DBL_MANT_MASK; // full mantissa -- see isnan/isinf note above
    if (exp == 0x7FF0000000000000ULL)
        return mant ? CCCC_FP_NAN : CCCC_FP_INFINITE;
    if (exp == 0)
        return mant ? CCCC_FP_SUBNORMAL : CCCC_FP_ZERO;
    return CCCC_FP_NORMAL;
}
static long long cccc_fpclassify_f(float x) {
    uint32_t u   = __cccc_c23_f2b(x);
    uint32_t exp = u & 0x7F800000U;
    uint32_t mant =
        u &
        __CCCC_C23_FLT_MANT_MASK; // full mantissa -- see isnan/isinf note above
    if (exp == 0x7F800000U)
        return mant ? CCCC_FP_NAN : CCCC_FP_INFINITE;
    if (exp == 0)
        return mant ? CCCC_FP_SUBNORMAL : CCCC_FP_ZERO;
    return CCCC_FP_NORMAL;
}

#ifdef __APPLE__
// __sincos[pi][f]_stret return a two-element struct by value, but the VM
// expects a struct return as a pointer to it (see wrap_div in stdlib.c): copy
// the result into a small rotating thread-local pool and return its slot.
#define CCCC_STRET_POOL_SLOTS 4
#define CCCC_STRET_WRAP(fn, arg_ty, ret_ty)                                    \
    static _Thread_local ret_ty fn##_pool[CCCC_STRET_POOL_SLOTS];              \
    static _Thread_local int    fn##_pool_idx;                                 \
    static long long wrap##fn(arg_ty x) {                                      \
        ret_ty *slot  = &fn##_pool[fn##_pool_idx];                             \
        fn##_pool_idx = (fn##_pool_idx + 1) % CCCC_STRET_POOL_SLOTS;           \
        *slot         = fn(x);                                                 \
        return (long long)(intptr_t)slot;                                      \
    }
CCCC_STRET_WRAP(__sincos_stret, double, struct __double2)
CCCC_STRET_WRAP(__sincospi_stret, double, struct __double2)
CCCC_STRET_WRAP(__sincosf_stret, float, struct __float2)
CCCC_STRET_WRAP(__sincospif_stret, float, struct __float2)
#endif

// Register all math.h functions
void register_math_functions(VirtualMachine *vm) {
    // Basic operations
    cc_register_cfunc_ex(vm, "fabs", (void *)fabs, 1, 1, 0b1);
    cc_register_cfunc(vm, "fabsf", (void *)fabsf, 1, 2);
    cc_register_cfunc_ex(vm, "fabsl", (void *)fabs, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "fmod", (void *)fmod, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmodf", (void *)fmodf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "fmodl", (void *)fmod, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remainder", (void *)remainder, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remainderf", (void *)remainderf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "remainderl", (void *)remainder, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remquo", (void *)remquo, 3, 1,
                         0b11); // double, double, int*
    cc_register_cfunc_ex(vm, "remquof", (void *)remquof, 3, 2, 0);
    cc_register_cfunc_ex(vm, "remquol", (void *)remquo, 3, 1, 0b11);
    cc_register_cfunc_ex(vm, "fma", (void *)fma, 3, 1,
                         0b111); // double, double, double
    cc_register_cfunc_ex(vm, "fmaf", (void *)fmaf, 3, 2, 0);
    cc_register_cfunc_ex(vm, "fmal", (void *)fma, 3, 1, 0b111);
    cc_register_cfunc_ex(vm, "fmax", (void *)fmax, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmaxf", (void *)fmaxf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "fmaxl", (void *)fmax, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmin", (void *)fmin, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fminf", (void *)fminf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "fminl", (void *)fmin, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fdim", (void *)fdim, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fdimf", (void *)fdimf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "fdiml", (void *)fdim, 2, 1, 0b11);
    cc_register_cfunc(vm, "nan", (void *)nan, 1, 1);
    cc_register_cfunc(vm, "nanf", (void *)nanf, 1, 2);
    cc_register_cfunc(vm, "nanl", (void *)nan, 1, 1);

    // Exponential/logarithmic - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "exp", (void *)exp, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expf", (void *)expf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "expl", (void *)exp, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "exp2", (void *)exp2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "exp2f", (void *)exp2f, 1, 2, 0);
    cc_register_cfunc_ex(vm, "exp2l", (void *)exp2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expm1", (void *)expm1, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expm1f", (void *)expm1f, 1, 2, 0);
    cc_register_cfunc_ex(vm, "expm1l", (void *)expm1, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log", (void *)log, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "logf", (void *)logf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "logl", (void *)log, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log10", (void *)log10, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log10f", (void *)log10f, 1, 2, 0);
    cc_register_cfunc_ex(vm, "log10l", (void *)log10, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log2", (void *)log2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log2f", (void *)log2f, 1, 2, 0);
    cc_register_cfunc_ex(vm, "log2l", (void *)log2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log1p", (void *)log1p, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log1pf", (void *)log1pf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "log1pl", (void *)log1p, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "pow", (void *)pow, 2, 1, 0b11); // double, double
    cc_register_cfunc_ex(vm, "powf", (void *)powf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "powl", (void *)pow, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "sqrt", (void *)sqrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sqrtf", (void *)sqrtf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "sqrtl", (void *)sqrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cbrt", (void *)cbrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cbrtf", (void *)cbrtf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "cbrtl", (void *)cbrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "hypot", (void *)hypot, 2, 1,
                         0b11); // double, double
    cc_register_cfunc_ex(vm, "hypotf", (void *)hypotf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "hypotl", (void *)hypot, 2, 1, 0b11);

    // Trigonometric - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "sin", (void *)sin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sinf", (void *)sinf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "sinl", (void *)sin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cos", (void *)cos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cosf", (void *)cosf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "cosl", (void *)cos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tan", (void *)tan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanf", (void *)tanf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "tanl", (void *)tan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asin", (void *)asin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinf", (void *)asinf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "asinl", (void *)asin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acos", (void *)acos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acosf", (void *)acosf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "acosl", (void *)acos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atan", (void *)atan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanf", (void *)atanf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "atanl", (void *)atan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atan2", (void *)atan2, 2, 1,
                         0b11); // double, double
    cc_register_cfunc_ex(vm, "atan2f", (void *)atan2f, 2, 2, 0);
    cc_register_cfunc_ex(vm, "atan2l", (void *)atan2, 2, 1, 0b11);

    // Hyperbolic - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "sinh", (void *)sinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sinhf", (void *)sinhf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "sinhl", (void *)sinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cosh", (void *)cosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "coshf", (void *)coshf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "coshl", (void *)cosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanh", (void *)tanh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanhf", (void *)tanhf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "tanhl", (void *)tanh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinh", (void *)asinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinhf", (void *)asinhf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "asinhl", (void *)asinh, 1, 1, 0b1);
    // acosh/atanh (#777): declared in include/math.h but never registered.
    cc_register_cfunc_ex(vm, "acosh", (void *)acosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acoshf", (void *)acoshf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "acoshl", (void *)acosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanh", (void *)atanh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanhf", (void *)atanhf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "atanhl", (void *)atanh, 1, 1, 0b1);

    // Special functions - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "erf", (void *)erf, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erff", (void *)erff, 1, 2, 0);
    cc_register_cfunc_ex(vm, "erfl", (void *)erf, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erfc", (void *)erfc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erfcf", (void *)erfcf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "erfcl", (void *)erfc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tgamma", (void *)tgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tgammaf", (void *)tgammaf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "tgammal", (void *)tgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lgamma", (void *)lgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lgammaf", (void *)lgammaf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "lgammal", (void *)lgamma, 1, 1, 0b1);

    // Rounding - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "ceil", (void *)ceil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "ceilf", (void *)ceilf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "ceill", (void *)ceil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "floor", (void *)floor, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "floorf", (void *)floorf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "floorl", (void *)floor, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "trunc", (void *)trunc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "truncf", (void *)truncf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "truncl", (void *)trunc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "round", (void *)round, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "roundf", (void *)roundf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "roundl", (void *)round, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lround", (void *)lround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lroundf", (void *)lroundf, 1, 0, 0);
    cc_register_cfunc_ex(vm, "lroundl", (void *)lround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llround", (void *)llround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llroundf", (void *)llroundf, 1, 0, 0);
    cc_register_cfunc_ex(vm, "llroundl", (void *)llround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "nearbyint", (void *)nearbyint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "nearbyintf", (void *)nearbyintf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "nearbyintl", (void *)nearbyint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "rint", (void *)rint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "rintf", (void *)rintf, 1, 2, 0);
    cc_register_cfunc_ex(vm, "rintl", (void *)rint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lrint", (void *)lrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lrintf", (void *)lrintf, 1, 0, 0);
    cc_register_cfunc_ex(vm, "lrintl", (void *)lrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llrint", (void *)llrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llrintf", (void *)llrintf, 1, 0, 0);
    cc_register_cfunc_ex(vm, "llrintl", (void *)llrint, 1, 0, 0b1);

    // Manipulation
    cc_register_cfunc_ex(vm, "frexp", (void *)frexp, 2, 1, 0b1);
    cc_register_cfunc(vm, "frexpf", (void *)frexpf, 2, 2);
    cc_register_cfunc_ex(vm, "frexpl", (void *)frexp, 2, 1, 0b1);
    cc_register_cfunc_ex(vm, "ldexp", (void *)ldexp, 2, 1, 0b01); // double, int
    cc_register_cfunc_ex(vm, "ldexpf", (void *)ldexpf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "ldexpl", (void *)ldexp, 2, 1, 0b01);
    cc_register_cfunc_ex(vm, "modf", (void *)modf, 2, 1,
                         0b1); // double, double*
    cc_register_cfunc_ex(vm, "modff", (void *)modff, 2, 2, 0);
    cc_register_cfunc_ex(vm, "modfl", (void *)modf, 2, 1, 0b1);
    cc_register_cfunc_ex(vm, "scalbn", (void *)scalbn, 2, 1,
                         0b01); // double, int
    cc_register_cfunc_ex(vm, "scalbnf", (void *)scalbnf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "scalbnl", (void *)scalbn, 2, 1, 0b01);
    cc_register_cfunc_ex(vm, "scalbln", (void *)scalbln, 2, 1,
                         0b01); // double, long
    cc_register_cfunc_ex(vm, "scalblnf", (void *)scalblnf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "scalblnl", (void *)scalbln, 2, 1, 0b01);
    cc_register_cfunc_ex(vm, "ilogb", (void *)ilogb, 1, 0, 0b1);
    cc_register_cfunc(vm, "ilogbf", (void *)ilogbf, 1, 0);
    cc_register_cfunc_ex(vm, "ilogbl", (void *)ilogb, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "logb", (void *)logb, 1, 1, 0b1);
    cc_register_cfunc(vm, "logbf", (void *)logbf, 1, 2);
    cc_register_cfunc_ex(vm, "logbl", (void *)logb, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "nextafter", (void *)nextafter, 2, 1,
                         0b11); // double, double
    cc_register_cfunc_ex(vm, "nextafterf", (void *)nextafterf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "nextafterl", (void *)nextafter, 2, 1, 0b11);
    // nexttoward(double, long double) / nexttowardf(float, long double): the
    // 2nd arg is long double which is 128-bit on Linux/aarch64.  The VM models
    // long double as 8 bytes, so we redirect to nextafter/nextafterf (same
    // return type, same double-ABI) to avoid the 128-bit host ABI mismatch
    // (#491).  nexttowardl already redirects to nextafter below. nexttowardf
    // uses a wrapper (cccc_nexttowardf) instead of nextafterf directly: CCCC
    // inserts an implicit cast on the long double arg (→ double), so CALLF sees
    // arg1 as double.  Passing a double to nextafterf(float,float) via libffi
    // gives the wrong low 32-bit interpretation on arm64 (#498).  The wrapper
    // accepts (float, double) and casts back to float before forwarding.
    cc_register_cfunc_ex(vm, "nexttoward", (void *)nextafter, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "nexttowardf", (void *)cccc_nexttowardf, 2, 2,
                         0b10);
    cc_register_cfunc_ex(vm, "nexttowardl", (void *)nextafter, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "copysign", (void *)copysign, 2, 1,
                         0b11); // double, double
    cc_register_cfunc_ex(vm, "copysignf", (void *)copysignf, 2, 2, 0);
    cc_register_cfunc_ex(vm, "copysignl", (void *)copysign, 2, 1, 0b11);

    // C23 exp10 family - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "exp10", (void *)__cccc_native_exp10, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "exp10f", (void *)__cccc_native_exp10f, 1, 2, 0b1);
    cc_register_cfunc_ex(vm, "exp10l", (void *)__cccc_native_exp10, 1, 1, 0b1);

    // C23 sinpi/cospi/tanpi family - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "sinpi", (void *)__cccc_native_sinpi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sinpif", (void *)__cccc_native_sinpif, 1, 2, 0b1);
    cc_register_cfunc_ex(vm, "sinpil", (void *)__cccc_native_sinpi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cospi", (void *)__cccc_native_cospi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cospif", (void *)__cccc_native_cospif, 1, 2, 0b1);
    cc_register_cfunc_ex(vm, "cospil", (void *)__cccc_native_cospi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanpi", (void *)__cccc_native_tanpi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanpif", (void *)__cccc_native_tanpif, 1, 2, 0b1);
    cc_register_cfunc_ex(vm, "tanpil", (void *)__cccc_native_tanpi, 1, 1, 0b1);

    // C23 asinpi/acospi/atanpi/atan2pi family
    cc_register_cfunc_ex(vm, "asinpi", (void *)__cccc_native_asinpi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinpif", (void *)__cccc_native_asinpif, 1, 2,
                         0b1);
    cc_register_cfunc_ex(vm, "asinpil", (void *)__cccc_native_asinpi, 1, 1,
                         0b1);
    cc_register_cfunc_ex(vm, "acospi", (void *)__cccc_native_acospi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acospif", (void *)__cccc_native_acospif, 1, 2,
                         0b1);
    cc_register_cfunc_ex(vm, "acospil", (void *)__cccc_native_acospi, 1, 1,
                         0b1);
    cc_register_cfunc_ex(vm, "atanpi", (void *)__cccc_native_atanpi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanpif", (void *)__cccc_native_atanpif, 1, 2,
                         0b1);
    cc_register_cfunc_ex(vm, "atanpil", (void *)__cccc_native_atanpi, 1, 1,
                         0b1);
    cc_register_cfunc_ex(vm, "atan2pi", (void *)__cccc_native_atan2pi, 2, 1,
                         0b11); // double, double
    cc_register_cfunc_ex(vm, "atan2pif", (void *)__cccc_native_atan2pif, 2, 2,
                         0b11);
    cc_register_cfunc_ex(vm, "atan2pil", (void *)__cccc_native_atan2pi, 2, 1,
                         0b11);

    // C23 IEC 60559:2020 interchange functions (#774)
#define CCCC_REG_FMAXIMUM_FAMILY(NAME)                                         \
    cc_register_cfunc_ex(vm, #NAME, (void *)__cccc_native_##NAME, 2, 1, 0b11); \
    cc_register_cfunc_ex(vm, #NAME "f", (void *)__cccc_native_##NAME##f, 2, 2, \
                         0b11);                                                \
    cc_register_cfunc_ex(vm, #NAME "l", (void *)__cccc_native_##NAME, 2, 1,    \
                         0b11);
    CCCC_REG_FMAXIMUM_FAMILY(fmaximum)
    CCCC_REG_FMAXIMUM_FAMILY(fminimum)
    CCCC_REG_FMAXIMUM_FAMILY(fmaximum_num)
    CCCC_REG_FMAXIMUM_FAMILY(fminimum_num)
    CCCC_REG_FMAXIMUM_FAMILY(fmaximum_mag)
    CCCC_REG_FMAXIMUM_FAMILY(fminimum_mag)
    CCCC_REG_FMAXIMUM_FAMILY(fmaximum_mag_num)
    CCCC_REG_FMAXIMUM_FAMILY(fminimum_mag_num)
#undef CCCC_REG_FMAXIMUM_FAMILY

    cc_register_cfunc_ex(vm, "totalorder", (void *)cccc_totalorder, 2, 0,
                         0b00); // const double*, const double*
    cc_register_cfunc_ex(vm, "totalorderf", (void *)cccc_totalorderf, 2, 0,
                         0b00); // const float*, const float*
    cc_register_cfunc_ex(vm, "totalorderl", (void *)cccc_totalorder, 2, 0,
                         0b00);
    cc_register_cfunc_ex(vm, "totalordermag", (void *)cccc_totalordermag, 2, 0,
                         0b00);
    cc_register_cfunc_ex(vm, "totalordermagf", (void *)cccc_totalordermagf, 2,
                         0, 0b00);
    cc_register_cfunc_ex(vm, "totalordermagl", (void *)cccc_totalordermag, 2, 0,
                         0b00);

    cc_register_cfunc_ex(vm, "canonicalize", (void *)cccc_canonicalize, 2, 0,
                         0b00); // double*, const double*
    cc_register_cfunc_ex(vm, "canonicalizef", (void *)cccc_canonicalizef, 2, 0,
                         0b00); // float*, const float*
    cc_register_cfunc_ex(vm, "canonicalizel", (void *)cccc_canonicalize, 2, 0,
                         0b00);

    cc_register_cfunc_ex(vm, "getpayload", (void *)cccc_getpayload, 1, 1,
                         0); // const double* -> double
    cc_register_cfunc_ex(vm, "getpayloadf", (void *)cccc_getpayloadf, 1, 2,
                         0); // const float* -> float
    cc_register_cfunc_ex(vm, "getpayloadl", (void *)cccc_getpayload, 1, 1, 0);

    cc_register_cfunc_ex(vm, "setpayload", (void *)cccc_setpayload, 2, 0,
                         0b10); // double*, double
    cc_register_cfunc_ex(vm, "setpayloadf", (void *)cccc_setpayloadf, 2, 0,
                         0b10); // float*, float
    cc_register_cfunc_ex(vm, "setpayloadl", (void *)cccc_setpayload, 2, 0,
                         0b10);
    cc_register_cfunc_ex(vm, "setpayloadsig", (void *)cccc_setpayloadsig, 2, 0,
                         0b10);
    cc_register_cfunc_ex(vm, "setpayloadsigf", (void *)cccc_setpayloadsigf, 2,
                         0, 0b10);
    cc_register_cfunc_ex(vm, "setpayloadsigl", (void *)cccc_setpayloadsig, 2, 0,
                         0b10);

    cc_register_cfunc_ex(vm, "llogb", (void *)__cccc_native_llogb, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llogbf", (void *)__cccc_native_llogbf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llogbl", (void *)__cccc_native_llogb, 1, 0, 0b1);

    // fromfp/ufromfp/fromfpx/ufromfpx return intmax_t/uintmax_t (long
    // long, not double/float) -- only arg0 (x) is FP-class, so ret=0 with
    // mask 0b1 for all three width variants.
#define CCCC_REG_FROMFP_FAMILY(NAME)                                           \
    cc_register_cfunc_ex(vm, #NAME, (void *)__cccc_native_##NAME, 3, 0, 0b1);  \
    cc_register_cfunc_ex(vm, #NAME "f", (void *)__cccc_native_##NAME##f, 3, 0, \
                         0b1);                                                 \
    cc_register_cfunc_ex(vm, #NAME "l", (void *)__cccc_native_##NAME, 3, 0,    \
                         0b1);
    CCCC_REG_FROMFP_FAMILY(fromfp)
    CCCC_REG_FROMFP_FAMILY(ufromfp)
    CCCC_REG_FROMFP_FAMILY(fromfpx)
    CCCC_REG_FROMFP_FAMILY(ufromfpx)
#undef CCCC_REG_FROMFP_FAMILY

    cc_register_cfunc_ex(vm, "__cccc_issignaling_f", (void *)cccc_issignaling_f,
                         1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_issignaling_d", (void *)cccc_issignaling_d,
                         1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_iseqsig_f", (void *)cccc_iseqsig_f, 2, 0,
                         0b11);
    cc_register_cfunc_ex(vm, "__cccc_iseqsig_d", (void *)cccc_iseqsig_d, 2, 0,
                         0b11);

    // Classification macro backing functions (#778)
    cc_register_cfunc_ex(vm, "__cccc_isnan_f", (void *)cccc_isnan_f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_isnan_d", (void *)cccc_isnan_d, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_isinf_f", (void *)cccc_isinf_f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_isinf_d", (void *)cccc_isinf_d, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_signbit_f", (void *)cccc_signbit_f, 1, 0,
                         0b1);
    cc_register_cfunc_ex(vm, "__cccc_signbit_d", (void *)cccc_signbit_d, 1, 0,
                         0b1);
    cc_register_cfunc_ex(vm, "__cccc_fpclassify_f", (void *)cccc_fpclassify_f,
                         1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_fpclassify_d", (void *)cccc_fpclassify_d,
                         1, 0, 0b1);
#ifdef __APPLE__
    cc_register_cfunc_ex(vm, "__sincos_stret", (void *)wrap__sincos_stret, 1, 0,
                         0b1);
    cc_register_cfunc_ex(vm, "__sincospi_stret", (void *)wrap__sincospi_stret,
                         1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__sincosf_stret", (void *)wrap__sincosf_stret, 1,
                         0, 0);
    cc_register_cfunc_ex(vm, "__sincospif_stret", (void *)wrap__sincospif_stret,
                         1, 0, 0);
#endif
}
