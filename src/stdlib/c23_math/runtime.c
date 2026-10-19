// C23 <math.h> functions absent from some host libms: exp10, the pi-scaled
// trig family and the IEC 60559 interchange functions. Compiled into the VM
// by src/stdlib/math.c and emitted verbatim as -c=native/-m shim text, so
// both run the same code. Guest calls are renamed to __cccc_native_<name>.
//
// The text never includes <math.h> and only uses math through __builtin_*:
// native output already has a host or bundled math.h in scope, and a second
// one clashes. `l` variants narrow to double, since CCCC models long double
// as double; their pointers address 8-byte guest storage.
#include <fenv.h>
#include <stdint.h>
#include <string.h>

#if defined(__APPLE__)
// Private libm symbols, exported since macOS 10.9 but never declared.
extern double __exp10(double);
extern float __exp10f(float);
extern double __sinpi(double);
extern float __sinpif(float);
extern double __cospi(double);
extern float __cospif(float);
extern double __tanpi(double);
extern float __tanpif(float);
#elif defined(__GLIBC__)
// Declared by glibc's math.h only under _GNU_SOURCE.
extern double exp10(double);
extern float exp10f(float);
#endif

#define __CCCC_C23_PI  3.14159265358979323846
#define __CCCC_C23_PIf 3.14159265358979323846f

// ---- exp10 ----

static inline double __cccc_native_exp10(double x) {
#if defined(__APPLE__)
    return __exp10(x);
#elif defined(__GLIBC__)
    return exp10(x);
#else
    return __builtin_pow(10.0, x);
#endif
}
static inline float __cccc_native_exp10f(float x) {
#if defined(__APPLE__)
    return __exp10f(x);
#elif defined(__GLIBC__)
    return exp10f(x);
#else
    return __builtin_powf(10.0f, x);
#endif
}

// ---- sinpi/cospi/tanpi ----
// Integer and half-integer arguments are special-cased so results are exact
// (sin(x * pi) is not, because pi itself is rounded).

static inline double __cccc_native_sinpi(double x) {
#if defined(__APPLE__)
    return __sinpi(x);
#else
    double r = __builtin_fmod(x, 2.0);
    if (r == 0.0 || r == 1.0 || r == -1.0)
        return __builtin_copysign(0.0, x);
    if (r == 0.5 || r == -1.5)
        return 1.0;
    if (r == -0.5 || r == 1.5)
        return -1.0;
    return __builtin_sin(x * __CCCC_C23_PI);
#endif
}
static inline float __cccc_native_sinpif(float x) {
#if defined(__APPLE__)
    return __sinpif(x);
#else
    float r = __builtin_fmodf(x, 2.0f);
    if (r == 0.0f || r == 1.0f || r == -1.0f)
        return __builtin_copysignf(0.0f, x);
    if (r == 0.5f || r == -1.5f)
        return 1.0f;
    if (r == -0.5f || r == 1.5f)
        return -1.0f;
    return __builtin_sinf(x * __CCCC_C23_PIf);
#endif
}
static inline double __cccc_native_cospi(double x) {
#if defined(__APPLE__)
    return __cospi(x);
#else
    double r = __builtin_fmod(x, 2.0);
    if (r < 0.0)
        r += 2.0;
    if (r == 0.0)
        return 1.0;
    if (r == 1.0)
        return -1.0;
    if (r == 0.5 || r == 1.5)
        return 0.0;
    return __builtin_cos(x * __CCCC_C23_PI);
#endif
}
static inline float __cccc_native_cospif(float x) {
#if defined(__APPLE__)
    return __cospif(x);
#else
    float r = __builtin_fmodf(x, 2.0f);
    if (r < 0.0f)
        r += 2.0f;
    if (r == 0.0f)
        return 1.0f;
    if (r == 1.0f)
        return -1.0f;
    if (r == 0.5f || r == 1.5f)
        return 0.0f;
    return __builtin_cosf(x * __CCCC_C23_PIf);
#endif
}
static inline double __cccc_native_tanpi(double x) {
#if defined(__APPLE__)
    return __tanpi(x);
#else
    if (__builtin_fmod(x, 1.0) == 0.0)
        return __builtin_copysign(0.0, x);
    return __builtin_tan(x * __CCCC_C23_PI);
#endif
}
static inline float __cccc_native_tanpif(float x) {
#if defined(__APPLE__)
    return __tanpif(x);
#else
    if (__builtin_fmodf(x, 1.0f) == 0.0f)
        return __builtin_copysignf(0.0f, x);
    return __builtin_tanf(x * __CCCC_C23_PIf);
#endif
}

// ---- asinpi/acospi/atanpi/atan2pi ----

static inline double __cccc_native_asinpi(double x) {
    return __builtin_asin(x) / __CCCC_C23_PI;
}
static inline float __cccc_native_asinpif(float x) {
    return __builtin_asinf(x) / __CCCC_C23_PIf;
}
static inline double __cccc_native_acospi(double x) {
    return __builtin_acos(x) / __CCCC_C23_PI;
}
static inline float __cccc_native_acospif(float x) {
    return __builtin_acosf(x) / __CCCC_C23_PIf;
}
static inline double __cccc_native_atanpi(double x) {
    return __builtin_atan(x) / __CCCC_C23_PI;
}
static inline float __cccc_native_atanpif(float x) {
    return __builtin_atanf(x) / __CCCC_C23_PIf;
}
static inline double __cccc_native_atan2pi(double y, double x) {
    return __builtin_atan2(y, x) / __CCCC_C23_PI;
}
static inline float __cccc_native_atan2pif(float y, float x) {
    return __builtin_atan2f(y, x) / __CCCC_C23_PIf;
}

// ---- bit patterns ----

static inline uint64_t __cccc_c23_d2b(double x) {
    uint64_t u;
    memcpy(&u, &x, 8);
    return u;
}
static inline double __cccc_c23_b2d(uint64_t u) {
    double x;
    memcpy(&x, &u, 8);
    return x;
}
static inline uint32_t __cccc_c23_f2b(float x) {
    uint32_t u;
    memcpy(&u, &x, 4);
    return u;
}
static inline float __cccc_c23_b2f(uint32_t u) {
    float x;
    memcpy(&x, &u, 4);
    return x;
}

// The payload is every significand bit below the quiet bit.
#define __CCCC_C23_DBL_QUIET_BIT   0x0008000000000000ULL
#define __CCCC_C23_DBL_PAYLOAD_MAX 0x0007FFFFFFFFFFFFULL
#define __CCCC_C23_DBL_MANT_MASK   0x000FFFFFFFFFFFFFULL
#define __CCCC_C23_FLT_QUIET_BIT   0x00400000U
#define __CCCC_C23_FLT_PAYLOAD_MAX 0x003FFFFFU
#define __CCCC_C23_FLT_MANT_MASK   0x007FFFFFU

// ---- fmaximum/fminimum family ----
// Unlike fmax/fmin these propagate NaN and order -0 below +0. The _num
// variants return the other operand for a single NaN; the _mag variants
// compare magnitudes and fall back to the non-mag function on a tie.

#define __CCCC_C23_FMAXIMUM_FAMILY(SUF, T, FABS)                               \
    static inline T __cccc_native_fmaximum##SUF(T x, T y) {                    \
        if (__builtin_isnan(x))                                                \
            return x;                                                          \
        if (__builtin_isnan(y))                                                \
            return y;                                                          \
        if (x == y)                                                            \
            return (__builtin_signbit(x) && !__builtin_signbit(y)) ? y : x;    \
        return x > y ? x : y;                                                  \
    }                                                                          \
    static inline T __cccc_native_fminimum##SUF(T x, T y) {                    \
        if (__builtin_isnan(x))                                                \
            return x;                                                          \
        if (__builtin_isnan(y))                                                \
            return y;                                                          \
        if (x == y)                                                            \
            return (__builtin_signbit(x) && !__builtin_signbit(y)) ? x : y;    \
        return x < y ? x : y;                                                  \
    }                                                                          \
    static inline T __cccc_native_fmaximum_num##SUF(T x, T y) {                \
        if (__builtin_isnan(x) && __builtin_isnan(y))                          \
            return x;                                                          \
        if (__builtin_isnan(x))                                                \
            return y;                                                          \
        if (__builtin_isnan(y))                                                \
            return x;                                                          \
        return __cccc_native_fmaximum##SUF(x, y);                              \
    }                                                                          \
    static inline T __cccc_native_fminimum_num##SUF(T x, T y) {                \
        if (__builtin_isnan(x) && __builtin_isnan(y))                          \
            return x;                                                          \
        if (__builtin_isnan(x))                                                \
            return y;                                                          \
        if (__builtin_isnan(y))                                                \
            return x;                                                          \
        return __cccc_native_fminimum##SUF(x, y);                              \
    }                                                                          \
    static inline T __cccc_native_fmaximum_mag##SUF(T x, T y) {                \
        if (__builtin_isnan(x))                                                \
            return x;                                                          \
        if (__builtin_isnan(y))                                                \
            return y;                                                          \
        T ax = FABS(x), ay = FABS(y);                                          \
        if (ax == ay)                                                          \
            return __cccc_native_fmaximum##SUF(x, y);                          \
        return ax > ay ? x : y;                                                \
    }                                                                          \
    static inline T __cccc_native_fminimum_mag##SUF(T x, T y) {                \
        if (__builtin_isnan(x))                                                \
            return x;                                                          \
        if (__builtin_isnan(y))                                                \
            return y;                                                          \
        T ax = FABS(x), ay = FABS(y);                                          \
        if (ax == ay)                                                          \
            return __cccc_native_fminimum##SUF(x, y);                          \
        return ax < ay ? x : y;                                                \
    }                                                                          \
    static inline T __cccc_native_fmaximum_mag_num##SUF(T x, T y) {            \
        if (__builtin_isnan(x) && __builtin_isnan(y))                          \
            return x;                                                          \
        if (__builtin_isnan(x))                                                \
            return y;                                                          \
        if (__builtin_isnan(y))                                                \
            return x;                                                          \
        return __cccc_native_fmaximum_mag##SUF(x, y);                          \
    }                                                                          \
    static inline T __cccc_native_fminimum_mag_num##SUF(T x, T y) {            \
        if (__builtin_isnan(x) && __builtin_isnan(y))                          \
            return x;                                                          \
        if (__builtin_isnan(x))                                                \
            return y;                                                          \
        if (__builtin_isnan(y))                                                \
            return x;                                                          \
        return __cccc_native_fminimum_mag##SUF(x, y);                          \
    }

__CCCC_C23_FMAXIMUM_FAMILY(, double, __builtin_fabs)
__CCCC_C23_FMAXIMUM_FAMILY(f, float, __builtin_fabsf)

// ---- totalorder/totalordermag ----
// Flip every bit of a negative pattern and only the sign bit of a positive
// one; the keys then compare as unsigned integers in IEEE totalOrder,
// signaling NaNs included (hence the pointer parameters).

static inline uint64_t __cccc_c23_order_key64(uint64_t u) {
    return u ^ ((u & 0x8000000000000000ULL) ? 0xFFFFFFFFFFFFFFFFULL
                                            : 0x8000000000000000ULL);
}
static inline uint32_t __cccc_c23_order_key32(uint32_t u) {
    return u ^ ((u & 0x80000000U) ? 0xFFFFFFFFU : 0x80000000U);
}

static inline int __cccc_native_totalorder(const double *x, const double *y) {
    return __cccc_c23_order_key64(__cccc_c23_d2b(*x)) <=
           __cccc_c23_order_key64(__cccc_c23_d2b(*y));
}
static inline int __cccc_native_totalorderf(const float *x, const float *y) {
    return __cccc_c23_order_key32(__cccc_c23_f2b(*x)) <=
           __cccc_c23_order_key32(__cccc_c23_f2b(*y));
}
static inline int __cccc_native_totalordermag(const double *x,
                                              const double *y) {
    return __cccc_c23_order_key64(__cccc_c23_d2b(*x) & ~(1ULL << 63)) <=
           __cccc_c23_order_key64(__cccc_c23_d2b(*y) & ~(1ULL << 63));
}
static inline int __cccc_native_totalordermagf(const float *x, const float *y) {
    return __cccc_c23_order_key32(__cccc_c23_f2b(*x) & ~(1U << 31)) <=
           __cccc_c23_order_key32(__cccc_c23_f2b(*y) & ~(1U << 31));
}

// ---- canonicalize ----
// binary32/binary64 have no non-canonical encodings.

static inline int __cccc_native_canonicalize(double *cx, const double *x) {
    *cx = *x;
    return 0;
}
static inline int __cccc_native_canonicalizef(float *cx, const float *x) {
    *cx = *x;
    return 0;
}

// ---- getpayload/setpayload/setpayloadsig ----
// On failure the destination is zeroed, as setpayload(3) specifies. A
// signaling NaN needs a nonzero payload: with the quiet bit clear, a zero
// significand is infinity.

static inline double __cccc_native_getpayload(const double *x) {
    if (!__builtin_isnan(*x))
        return -1.0;
    return (double)(__cccc_c23_d2b(*x) & __CCCC_C23_DBL_PAYLOAD_MAX);
}
static inline float __cccc_native_getpayloadf(const float *x) {
    if (!__builtin_isnan(*x))
        return -1.0f;
    return (float)(__cccc_c23_f2b(*x) & __CCCC_C23_FLT_PAYLOAD_MAX);
}

static inline int __cccc_c23_setpayload(double *x, double payload, int quiet) {
    if (payload < 0.0 || payload != (double)(uint64_t)payload ||
        (uint64_t)payload > __CCCC_C23_DBL_PAYLOAD_MAX ||
        (!quiet && (uint64_t)payload == 0)) {
        *x = 0.0;
        return 1;
    }
    uint64_t bits = 0x7FF0000000000000ULL | (uint64_t)payload;
    if (quiet)
        bits |= __CCCC_C23_DBL_QUIET_BIT;
    *x = __cccc_c23_b2d(bits);
    return 0;
}
static inline int __cccc_c23_setpayloadf(float *x, float payload, int quiet) {
    if (payload < 0.0f || payload != (float)(uint32_t)payload ||
        (uint32_t)payload > __CCCC_C23_FLT_PAYLOAD_MAX ||
        (!quiet && (uint32_t)payload == 0)) {
        *x = 0.0f;
        return 1;
    }
    uint32_t bits = 0x7F800000U | (uint32_t)payload;
    if (quiet)
        bits |= __CCCC_C23_FLT_QUIET_BIT;
    *x = __cccc_c23_b2f(bits);
    return 0;
}
static inline int __cccc_native_setpayload(double *x, double payload) {
    return __cccc_c23_setpayload(x, payload, 1);
}
static inline int __cccc_native_setpayloadf(float *x, float payload) {
    return __cccc_c23_setpayloadf(x, payload, 1);
}
static inline int __cccc_native_setpayloadsig(double *x, double payload) {
    return __cccc_c23_setpayload(x, payload, 0);
}
static inline int __cccc_native_setpayloadsigf(float *x, float payload) {
    return __cccc_c23_setpayloadf(x, payload, 0);
}

// ---- llogb ----
// FP_ILOGB0/FP_ILOGBNAN widen unchanged.

static inline long __cccc_native_llogb(double x) {
    return __builtin_ilogb(x);
}
static inline long __cccc_native_llogbf(float x) {
    return __builtin_ilogbf(x);
}

// ---- fromfp/ufromfp/fromfpx/ufromfpx ----
// Round x in direction `rnd` (include/math.h's FP_INT_* values) and return
// it if it fits in `width` bits, else raise FE_INVALID and return 0. The x
// variants also raise FE_INEXACT when rounding changed the value. These use
// the TS 18661-1 integer-returning signature that include/math.h declares.

static inline double __cccc_c23_round_by_direction(double x, int rnd) {
    switch (rnd) {
        case 0:  /* FP_INT_UPWARD */
            return __builtin_ceil(x);
        case 1:  /* FP_INT_DOWNWARD */
            return __builtin_floor(x);
        case 2:  /* FP_INT_TOWARDZERO */
            return __builtin_trunc(x);
        case 3:  /* FP_INT_TONEARESTFROMZERO */
            return __builtin_round(x);
        default: /* FP_INT_TONEAREST */
            return __builtin_rint(x);
    }
}

static inline long long __cccc_c23_fromfp(double x, int rnd, unsigned int width,
                                          int is_unsigned, int extended) {
    if (__builtin_isnan(x) || __builtin_isinf(x) || width == 0) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    double r = __cccc_c23_round_by_direction(x, rnd);
    double lo, hi;
    if (is_unsigned) {
        lo = 0.0;
        hi = width >= 64 ? 18446744073709551615.0
                         : __builtin_ldexp(1.0, (int)width) - 1.0;
    } else {
        hi = width >= 64 ? 9223372036854775807.0
                         : __builtin_ldexp(1.0, (int)width - 1) - 1.0;
        lo = width >= 64 ? -9223372036854775808.0
                         : -__builtin_ldexp(1.0, (int)width - 1);
    }
    if (r < lo || r > hi) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    if (extended && r != x)
        feraiseexcept(FE_INEXACT);
    return is_unsigned ? (long long)(uint64_t)r : (long long)(int64_t)r;
}

#define __CCCC_C23_FROMFP(NAME, RET, T, UNS, EXT)                              \
    static inline RET __cccc_native_##NAME(T x, int rnd, unsigned int width) { \
        return (RET)__cccc_c23_fromfp((double)x, rnd, width, UNS, EXT);        \
    }
__CCCC_C23_FROMFP(fromfp, intmax_t, double, 0, 0)
__CCCC_C23_FROMFP(fromfpf, intmax_t, float, 0, 0)
__CCCC_C23_FROMFP(fromfpl, intmax_t, long double, 0, 0)
__CCCC_C23_FROMFP(ufromfp, uintmax_t, double, 1, 0)
__CCCC_C23_FROMFP(ufromfpf, uintmax_t, float, 1, 0)
__CCCC_C23_FROMFP(ufromfpl, uintmax_t, long double, 1, 0)
__CCCC_C23_FROMFP(fromfpx, intmax_t, double, 0, 1)
__CCCC_C23_FROMFP(fromfpxf, intmax_t, float, 0, 1)
__CCCC_C23_FROMFP(fromfpxl, intmax_t, long double, 0, 1)
__CCCC_C23_FROMFP(ufromfpx, uintmax_t, double, 1, 1)
__CCCC_C23_FROMFP(ufromfpxf, uintmax_t, float, 1, 1)
__CCCC_C23_FROMFP(ufromfpxl, uintmax_t, long double, 1, 1)
#undef __CCCC_C23_FROMFP

// ---- long double variants ----

#define __CCCC_C23_L1(NAME)                                                    \
    static inline long double __cccc_native_##NAME##l(long double x) {         \
        return __cccc_native_##NAME((double)x);                                \
    }
#define __CCCC_C23_L2(NAME)                                                    \
    static inline long double __cccc_native_##NAME##l(long double x,           \
                                                      long double y) {         \
        return __cccc_native_##NAME((double)x, (double)y);                     \
    }
__CCCC_C23_L1(exp10)
__CCCC_C23_L1(sinpi)
__CCCC_C23_L1(cospi)
__CCCC_C23_L1(tanpi)
__CCCC_C23_L1(asinpi)
__CCCC_C23_L1(acospi)
__CCCC_C23_L1(atanpi)
__CCCC_C23_L2(atan2pi)
__CCCC_C23_L2(fmaximum)
__CCCC_C23_L2(fminimum)
__CCCC_C23_L2(fmaximum_num)
__CCCC_C23_L2(fminimum_num)
__CCCC_C23_L2(fmaximum_mag)
__CCCC_C23_L2(fminimum_mag)
__CCCC_C23_L2(fmaximum_mag_num)
__CCCC_C23_L2(fminimum_mag_num)
#undef __CCCC_C23_L1
#undef __CCCC_C23_L2

static inline int __cccc_native_totalorderl(const long double *x,
                                            const long double *y) {
    return __cccc_native_totalorder((const double *)x, (const double *)y);
}
static inline int __cccc_native_totalordermagl(const long double *x,
                                               const long double *y) {
    return __cccc_native_totalordermag((const double *)x, (const double *)y);
}
static inline int __cccc_native_canonicalizel(long double       *cx,
                                              const long double *x) {
    return __cccc_native_canonicalize((double *)cx, (const double *)x);
}
static inline long double __cccc_native_getpayloadl(const long double *x) {
    return __cccc_native_getpayload((const double *)x);
}
static inline int __cccc_native_setpayloadl(long double *x,
                                            long double  payload) {
    return __cccc_native_setpayload((double *)x, (double)payload);
}
static inline int __cccc_native_setpayloadsigl(long double *x,
                                               long double  payload) {
    return __cccc_native_setpayloadsig((double *)x, (double)payload);
}
static inline long __cccc_native_llogbl(long double x) {
    return __cccc_native_llogb((double)x);
}
