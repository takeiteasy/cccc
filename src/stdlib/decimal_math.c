// _Decimal32/64/128 <math.h> transcendentals (tracker #828, phase 2 of #402):
// bid{32,64,128}_{sqrt,exp,log,pow,sin,cos,...} exposed as a real stdlib
// module (unlike src/stdlib/decimal.c's arithmetic shim, which is called
// directly from src/ops.c's D* opcodes and is NOT FFI-registered). Every
// entry point here goes through the ordinary cc_register_cfunc() path: a
// guest calls a typed wrapper (sqrtd64(), powd128(), ...) declared in
// include/decimal_math.h, which forwards to one of the six op-dispatch
// functions below by address, exactly like any other FFI call.
//
// Built without CCCC_HAS_DECIMAL, every entry point is a clean no-op
// (nothing written, -1 returned) rather than a silent binary-float
// fallback -- same policy as src/stdlib/decimal.c and the #824 "no lossy
// POSIX emulation" precedent. In practice this code path is unreachable
// from a guest: include/decimal_math.h's entire body is guarded by
// __STDC_IEC_60559_DFP__, so calling e.g. sqrtd64() without the library
// linked is a compile error, not a runtime one.
//
// Op numbering below MUST stay in sync with include/decimal_math.h's
// __CCCC_DECM_* macros -- there is no shared header between the two (the
// guest header can't include this file's internals), so a mismatch here is
// a silent miscompile, not a compile error. tests/suites/test_suite_decimal.c
// exercises every op at every width, which is what actually catches a
// mis-mapped op number.
//
// Performance placeholder: like decimal.c, every call here is an
// out-of-line hop into libbid with no inlining, fast paths, or
// register-level fusion. Acceptable for phase 2; follow-up filed as #833.

#include "../internal.h"
#include <string.h>

#ifdef CCCC_HAS_DECIMAL
#include "bid_conf.h" // must precede bid_functions.h (Intel's own contract)
#include "bid_functions.h"

static BID_UINT32  load32(const void *p)  { BID_UINT32 v;  memcpy(&v, p, 4);  return v; }
static BID_UINT64  load64(const void *p)  { BID_UINT64 v;  memcpy(&v, p, 8);  return v; }
static BID_UINT128 load128(const void *p) { BID_UINT128 v; memcpy(&v, p, 16); return v; }
static void store32(void *dst, BID_UINT32 v)   { memcpy(dst, &v, 4); }
static void store64(void *dst, BID_UINT64 v)   { memcpy(dst, &v, 8); }
static void store128(void *dst, BID_UINT128 v) { memcpy(dst, &v, 16); }

#define RND BID_ROUNDING_TO_NEAREST

// -- op numbers (mirrored in include/decimal_math.h) -------------------

// __cccc_dec_math1: unary, decimal result.
enum {
    OP1_SQRT = 1, OP1_CBRT, OP1_EXP, OP1_EXP2, OP1_EXP10, OP1_EXPM1,
    OP1_LOG, OP1_LOG2, OP1_LOG10, OP1_LOG1P,
    OP1_SIN, OP1_COS, OP1_TAN, OP1_ASIN, OP1_ACOS, OP1_ATAN,
    OP1_SINH, OP1_COSH, OP1_TANH, OP1_ASINH, OP1_ACOSH, OP1_ATANH,
    OP1_ERF, OP1_ERFC, OP1_LGAMMA, OP1_TGAMMA,
    OP1_CEIL, OP1_FLOOR, OP1_TRUNC, OP1_ROUND, OP1_NEARBYINT, OP1_RINT,
    OP1_LOGB, OP1_QUANTUM, OP1_FABS,
};

// __cccc_dec_math2: binary, decimal result.
enum {
    OP2_POW = 1, OP2_ATAN2, OP2_HYPOT, OP2_FDIM, OP2_QUANTIZE,
    OP2_FMOD, OP2_REMAINDER, OP2_FMIN, OP2_FMAX, OP2_NEXTAFTER,
    OP2_COPYSIGN, OP2_NEXTTOWARD, // NEXTTOWARD: b is always _Decimal128
};

// __cccc_dec_math3: ternary, decimal result.
enum { OP3_FMA = 1 };

// __cccc_dec_mathi: predicates + integer-valued conversions -> long long.
enum {
    OPI_ISNAN = 1, OPI_ISINF, OPI_ISFINITE, OPI_ISNORMAL, OPI_ISSIGNALING,
    OPI_SIGNBIT, OPI_FPCLASSIFY,
    OPI_ILOGB, OPI_QUANTEXP,
    OPI_LRINT, OPI_LLRINT, OPI_LROUND, OPI_LLROUND,
    OPI_SAMEQUANTUM, OPI_TOTALORDER, // use b
};

// __cccc_dec_mathn: decimal x int -> decimal (scale by power-of-radix n).
enum { OPN_SCALBN = 1, OPN_SCALBLN, OPN_LDEXP };

// __cccc_dec_mathp: unary with an out-parameter.
enum { OPP_FREXP = 1, OPP_MODF };

// fpclassify values, mirrored from include/math.h (FP_INFINITE=1, FP_NAN=2,
// FP_NORMAL=3, FP_SUBNORMAL=4, FP_ZERO=5) -- kept numeric here rather than
// pulling in math.h from a stdlib .c file that isn't otherwise coupled to it.
enum { CCCC_FP_INFINITE = 1, CCCC_FP_NAN = 2, CCCC_FP_NORMAL = 3,
       CCCC_FP_SUBNORMAL = 4, CCCC_FP_ZERO = 5 };

// -- __cccc_dec_math1: one static per-width worker, generated 3x by macro --

#define MK_MATH1(SFX, BIDT, LD, ST) \
static long long dec_math1_##SFX(long long op, long long dst, long long a) { \
    unsigned f = 0; \
    BIDT x = LD((const void *)a); \
    BIDT r; \
    switch (op) { \
    case OP1_SQRT:      r = __bid##SFX##_sqrt(x, RND, &f); break; \
    case OP1_CBRT:      r = __bid##SFX##_cbrt(x, RND, &f); break; \
    case OP1_EXP:       r = __bid##SFX##_exp(x, RND, &f); break; \
    case OP1_EXP2:      r = __bid##SFX##_exp2(x, RND, &f); break; \
    case OP1_EXP10:     r = __bid##SFX##_exp10(x, RND, &f); break; \
    case OP1_EXPM1:     r = __bid##SFX##_expm1(x, RND, &f); break; \
    case OP1_LOG:       r = __bid##SFX##_log(x, RND, &f); break; \
    case OP1_LOG2:      r = __bid##SFX##_log2(x, RND, &f); break; \
    case OP1_LOG10:     r = __bid##SFX##_log10(x, RND, &f); break; \
    case OP1_LOG1P:     r = __bid##SFX##_log1p(x, RND, &f); break; \
    case OP1_SIN:       r = __bid##SFX##_sin(x, RND, &f); break; \
    case OP1_COS:       r = __bid##SFX##_cos(x, RND, &f); break; \
    case OP1_TAN:       r = __bid##SFX##_tan(x, RND, &f); break; \
    case OP1_ASIN:      r = __bid##SFX##_asin(x, RND, &f); break; \
    case OP1_ACOS:      r = __bid##SFX##_acos(x, RND, &f); break; \
    case OP1_ATAN:      r = __bid##SFX##_atan(x, RND, &f); break; \
    case OP1_SINH:      r = __bid##SFX##_sinh(x, RND, &f); break; \
    case OP1_COSH:      r = __bid##SFX##_cosh(x, RND, &f); break; \
    case OP1_TANH:      r = __bid##SFX##_tanh(x, RND, &f); break; \
    case OP1_ASINH:     r = __bid##SFX##_asinh(x, RND, &f); break; \
    case OP1_ACOSH:     r = __bid##SFX##_acosh(x, RND, &f); break; \
    case OP1_ATANH:     r = __bid##SFX##_atanh(x, RND, &f); break; \
    case OP1_ERF:       r = __bid##SFX##_erf(x, RND, &f); break; \
    case OP1_ERFC:      r = __bid##SFX##_erfc(x, RND, &f); break; \
    case OP1_LGAMMA:    r = __bid##SFX##_lgamma(x, RND, &f); break; \
    case OP1_TGAMMA:    r = __bid##SFX##_tgamma(x, RND, &f); break; \
    case OP1_CEIL:      r = __bid##SFX##_round_integral_positive(x, &f); break; \
    case OP1_FLOOR:     r = __bid##SFX##_round_integral_negative(x, &f); break; \
    case OP1_TRUNC:     r = __bid##SFX##_round_integral_zero(x, &f); break; \
    case OP1_ROUND:     r = __bid##SFX##_round_integral_nearest_away(x, &f); break; \
    case OP1_NEARBYINT: r = __bid##SFX##_round_integral_nearest_even(x, &f); break; \
    case OP1_RINT:      r = __bid##SFX##_round_integral_exact(x, RND, &f); break; \
    case OP1_LOGB:      r = __bid##SFX##_logb(x, &f); break; \
    case OP1_QUANTUM:   r = __bid##SFX##_quantum(x, &f); break; \
    case OP1_FABS:      r = __bid##SFX##_abs(x); break; \
    default: return -1; \
    } \
    ST((void *)dst, r); \
    return 0; \
}
MK_MATH1(32, BID_UINT32, load32, store32)
MK_MATH1(64, BID_UINT64, load64, store64)
MK_MATH1(128, BID_UINT128, load128, store128)
#undef MK_MATH1

long long __cccc_dec_math1(long long op, long long w, long long dst, long long a) {
    switch (w) {
    case 0: return dec_math1_32(op, dst, a);
    case 1: return dec_math1_64(op, dst, a);
    case 2: return dec_math1_128(op, dst, a);
    default: return -1;
    }
}

// -- __cccc_dec_math2 ----------------------------------------------------

#define MK_MATH2(SFX, BIDT, LD, ST) \
static long long dec_math2_##SFX(long long op, long long dst, long long a, long long b) { \
    unsigned f = 0; \
    BIDT x = LD((const void *)a); \
    BIDT r; \
    switch (op) { \
    case OP2_POW:      r = __bid##SFX##_pow(x, LD((const void *)b), RND, &f); break; \
    case OP2_ATAN2:    r = __bid##SFX##_atan2(x, LD((const void *)b), RND, &f); break; \
    case OP2_HYPOT:    r = __bid##SFX##_hypot(x, LD((const void *)b), RND, &f); break; \
    case OP2_FDIM:     r = __bid##SFX##_fdim(x, LD((const void *)b), RND, &f); break; \
    case OP2_QUANTIZE: r = __bid##SFX##_quantize(x, LD((const void *)b), RND, &f); break; \
    case OP2_FMOD:     r = __bid##SFX##_fmod(x, LD((const void *)b), &f); break; \
    case OP2_REMAINDER:r = __bid##SFX##_rem(x, LD((const void *)b), &f); break; \
    case OP2_FMIN:     r = __bid##SFX##_minnum(x, LD((const void *)b), &f); break; \
    case OP2_FMAX:     r = __bid##SFX##_maxnum(x, LD((const void *)b), &f); break; \
    case OP2_NEXTAFTER:r = __bid##SFX##_nextafter(x, LD((const void *)b), &f); break; \
    case OP2_COPYSIGN: r = __bid##SFX##_copySign(x, LD((const void *)b)); break; \
    case OP2_NEXTTOWARD: r = __bid##SFX##_nexttoward(x, load128((const void *)b), &f); break; \
    default: return -1; \
    } \
    ST((void *)dst, r); \
    return 0; \
}
MK_MATH2(32, BID_UINT32, load32, store32)
MK_MATH2(64, BID_UINT64, load64, store64)
MK_MATH2(128, BID_UINT128, load128, store128)
#undef MK_MATH2

long long __cccc_dec_math2(long long op, long long w, long long dst, long long a, long long b) {
    switch (w) {
    case 0: return dec_math2_32(op, dst, a, b);
    case 1: return dec_math2_64(op, dst, a, b);
    case 2: return dec_math2_128(op, dst, a, b);
    default: return -1;
    }
}

// -- __cccc_dec_math3: fma is the only ternary op -------------------------

#define MK_MATH3(SFX, BIDT, LD, ST) \
static long long dec_math3_##SFX(long long op, long long dst, long long a, long long b, long long c) { \
    unsigned f = 0; \
    if (op != OP3_FMA) return -1; \
    BIDT r = __bid##SFX##_fma(LD((const void *)a), LD((const void *)b), LD((const void *)c), RND, &f); \
    ST((void *)dst, r); \
    return 0; \
}
MK_MATH3(32, BID_UINT32, load32, store32)
MK_MATH3(64, BID_UINT64, load64, store64)
MK_MATH3(128, BID_UINT128, load128, store128)
#undef MK_MATH3

long long __cccc_dec_math3(long long op, long long w, long long dst, long long a, long long b, long long c) {
    switch (w) {
    case 0: return dec_math3_32(op, dst, a, b, c);
    case 1: return dec_math3_64(op, dst, a, b, c);
    case 2: return dec_math3_128(op, dst, a, b, c);
    default: return -1;
    }
}

// -- __cccc_dec_mathi: predicates + integer-valued conversions -----------

#define MK_MATHI(SFX, BIDT, LD) \
static long long dec_mathi_##SFX(long long op, long long a, long long b) { \
    unsigned f = 0; \
    BIDT x = LD((const void *)a); \
    switch (op) { \
    case OPI_ISNAN:       return __bid##SFX##_isNaN(x); \
    case OPI_ISINF:       return __bid##SFX##_isInf(x); \
    case OPI_ISFINITE:    return __bid##SFX##_isFinite(x); \
    case OPI_ISNORMAL:    return __bid##SFX##_isNormal(x); \
    case OPI_ISSIGNALING: return __bid##SFX##_isSignaling(x); \
    case OPI_SIGNBIT:     return __bid##SFX##_isSigned(x); \
    case OPI_FPCLASSIFY: \
        if (__bid##SFX##_isNaN(x)) return CCCC_FP_NAN; \
        if (__bid##SFX##_isInf(x)) return CCCC_FP_INFINITE; \
        if (__bid##SFX##_isZero(x)) return CCCC_FP_ZERO; \
        if (__bid##SFX##_isSubnormal(x)) return CCCC_FP_SUBNORMAL; \
        return CCCC_FP_NORMAL; \
    case OPI_ILOGB:       return __bid##SFX##_ilogb(x, &f); \
    case OPI_QUANTEXP:    return __bid##SFX##_quantexp(x, &f); \
    case OPI_LRINT:       return __bid##SFX##_lrint(x, RND, &f); \
    case OPI_LLRINT:      return __bid##SFX##_llrint(x, RND, &f); \
    case OPI_LROUND:      return __bid##SFX##_lround(x, &f); \
    case OPI_LLROUND:     return __bid##SFX##_llround(x, &f); \
    case OPI_SAMEQUANTUM: return __bid##SFX##_sameQuantum(x, LD((const void *)b)); \
    case OPI_TOTALORDER:  return __bid##SFX##_totalOrder(x, LD((const void *)b)); \
    default: return -1; \
    } \
}
MK_MATHI(32, BID_UINT32, load32)
MK_MATHI(64, BID_UINT64, load64)
MK_MATHI(128, BID_UINT128, load128)
#undef MK_MATHI

long long __cccc_dec_mathi(long long op, long long w, long long a, long long b) {
    switch (w) {
    case 0: return dec_mathi_32(op, a, b);
    case 1: return dec_mathi_64(op, a, b);
    case 2: return dec_mathi_128(op, a, b);
    default: return -1;
    }
}

// -- __cccc_dec_mathn: decimal x int -> decimal ---------------------------

#define MK_MATHN(SFX, BIDT, LD, ST) \
static long long dec_mathn_##SFX(long long op, long long dst, long long a, long long n) { \
    unsigned f = 0; \
    BIDT x = LD((const void *)a); \
    BIDT r; \
    switch (op) { \
    case OPN_SCALBN:  r = __bid##SFX##_scalbn(x, (int)n, RND, &f); break; \
    case OPN_SCALBLN: r = __bid##SFX##_scalbln(x, (long)n, RND, &f); break; \
    case OPN_LDEXP:   r = __bid##SFX##_ldexp(x, (int)n, RND, &f); break; \
    default: return -1; \
    } \
    ST((void *)dst, r); \
    return 0; \
}
MK_MATHN(32, BID_UINT32, load32, store32)
MK_MATHN(64, BID_UINT64, load64, store64)
MK_MATHN(128, BID_UINT128, load128, store128)
#undef MK_MATHN

long long __cccc_dec_mathn(long long op, long long w, long long dst, long long a, long long n) {
    switch (w) {
    case 0: return dec_mathn_32(op, dst, a, n);
    case 1: return dec_mathn_64(op, dst, a, n);
    case 2: return dec_mathn_128(op, dst, a, n);
    default: return -1;
    }
}

// -- __cccc_dec_mathp: frexp (int* out)/modf (decimal* out) --------------

#define MK_MATHP(SFX, BIDT, LD, ST) \
static long long dec_mathp_##SFX(long long op, long long dst, long long a, long long outp) { \
    unsigned f = 0; \
    BIDT x = LD((const void *)a); \
    BIDT r; \
    switch (op) { \
    case OPP_FREXP: { \
        int exp = 0; \
        r = __bid##SFX##_frexp(x, &exp); \
        *(int *)(void *)outp = exp; \
        break; \
    } \
    case OPP_MODF: { \
        BIDT ip; \
        r = __bid##SFX##_modf(x, &ip, &f); \
        ST((void *)outp, ip); \
        break; \
    } \
    default: return -1; \
    } \
    ST((void *)dst, r); \
    return 0; \
}
MK_MATHP(32, BID_UINT32, load32, store32)
MK_MATHP(64, BID_UINT64, load64, store64)
MK_MATHP(128, BID_UINT128, load128, store128)
#undef MK_MATHP

long long __cccc_dec_mathp(long long op, long long w, long long dst, long long a, long long outp) {
    switch (w) {
    case 0: return dec_mathp_32(op, dst, a, outp);
    case 1: return dec_mathp_64(op, dst, a, outp);
    case 2: return dec_mathp_128(op, dst, a, outp);
    default: return -1;
    }
}

#else // !CCCC_HAS_DECIMAL

long long __cccc_dec_math1(long long op, long long w, long long dst, long long a) {
    (void)op; (void)w; (void)dst; (void)a;
    return -1;
}
long long __cccc_dec_math2(long long op, long long w, long long dst, long long a, long long b) {
    (void)op; (void)w; (void)dst; (void)a; (void)b;
    return -1;
}
long long __cccc_dec_math3(long long op, long long w, long long dst, long long a, long long b, long long c) {
    (void)op; (void)w; (void)dst; (void)a; (void)b; (void)c;
    return -1;
}
long long __cccc_dec_mathi(long long op, long long w, long long a, long long b) {
    (void)op; (void)w; (void)a; (void)b;
    return -1;
}
long long __cccc_dec_mathn(long long op, long long w, long long dst, long long a, long long n) {
    (void)op; (void)w; (void)dst; (void)a; (void)n;
    return -1;
}
long long __cccc_dec_mathp(long long op, long long w, long long dst, long long a, long long outp) {
    (void)op; (void)w; (void)dst; (void)a; (void)outp;
    return -1;
}

#endif // CCCC_HAS_DECIMAL

void register_decimal_math_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "__cccc_dec_math1", (void*)__cccc_dec_math1, 4, 0);
    cc_register_cfunc(vm, "__cccc_dec_math2", (void*)__cccc_dec_math2, 5, 0);
    cc_register_cfunc(vm, "__cccc_dec_math3", (void*)__cccc_dec_math3, 6, 0);
    cc_register_cfunc(vm, "__cccc_dec_mathi", (void*)__cccc_dec_mathi, 4, 0);
    cc_register_cfunc(vm, "__cccc_dec_mathn", (void*)__cccc_dec_mathn, 5, 0);
    cc_register_cfunc(vm, "__cccc_dec_mathp", (void*)__cccc_dec_mathp, 5, 0);
}
