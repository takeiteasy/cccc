// math.h stdlib function registration
#include "../cccc.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <fenv.h>

// Host-side mirrors of the FP_INT_* rounding-direction constants declared
// in include/math.h (guest side) for the fromfp/ufromfp family -- this file
// is compiled by the host toolchain, not cccc, so it can't see the guest
// header's macros; the numeric values must match exactly.
#define CCCC_FP_INT_UPWARD 0
#define CCCC_FP_INT_DOWNWARD 1
#define CCCC_FP_INT_TOWARDZERO 2
#define CCCC_FP_INT_TONEARESTFROMZERO 3
#define CCCC_FP_INT_TONEAREST 4

// nexttowardf(float, long double): CCCC promotes the long double arg to double
// (the VM's long double width), so we need a wrapper that accepts (float, double)
// and casts back to float before delegating to nextafterf (#498).
static float cccc_nexttowardf(float x, double y) {
    return nextafterf(x, (float)y);
}

#if defined(__APPLE__)
// Private Apple libm symbols backing the C23 exp10/pi-trig family,
// available since macOS 10.9 (no public declarations exist).
extern double __exp10(double);
extern float __exp10f(float);
extern double __sinpi(double);
extern float __sinpif(float);
extern double __cospi(double);
extern float __cospif(float);
extern double __tanpi(double);
extern float __tanpif(float);
#endif

#if defined(__GLIBC__)
// exp10/exp10f/exp10l are GNU extensions present in libm but only declared
// by glibc's math.h under _GNU_SOURCE, which this build does not define.
extern double exp10(double);
extern float exp10f(float);
extern long double exp10l(long double);
#endif

// pi constants (math.h does not define M_PI)
#define CCCC_PIl 3.14159265358979323846264338327950288419716939937510582097494L
#define CCCC_PI  ((double)CCCC_PIl)
#define CCCC_PIf ((float)CCCC_PIl)

// ---- exp10 family (C23) ----
#if defined(__APPLE__)
static double cccc_exp10(double x)  { return __exp10(x); }
static float  cccc_exp10f(float x)  { return __exp10f(x); }
#elif defined(__GLIBC__)
static double cccc_exp10(double x)  { return exp10(x); }
static float  cccc_exp10f(float x)  { return exp10f(x); }
#else
static double cccc_exp10(double x)  { return pow(10.0, x); }
static float  cccc_exp10f(float x)  { return powf(10.0f, x); }
#endif

// Long double exp10l bridged at double precision (VM models long double as
// 8 bytes; using the double ABI avoids a 128-bit host ABI mismatch on Linux
// aarch64 where long double is 128-bit — #491).
static double cccc_exp10l(double x) { return cccc_exp10(x); }

// ---- sinpi/cospi/tanpi family (C23) ----
// On non-Apple platforms, integer/half-integer arguments are special-cased
// so results are exact (matching Apple's __sinpi/__cospi/__tanpi), since a
// naive sin(x * pi)-style computation loses precision from the imprecision
// of pi itself (e.g. sin(M_PI) != 0.0).
#if defined(__APPLE__)
static double cccc_sinpi(double x) { return __sinpi(x); }
static float  cccc_sinpif(float x) { return __sinpif(x); }
static double cccc_cospi(double x) { return __cospi(x); }
static float  cccc_cospif(float x) { return __cospif(x); }
static double cccc_tanpi(double x) { return __tanpi(x); }
static float  cccc_tanpif(float x) { return __tanpif(x); }
#else
static double cccc_sinpi(double x) {
    double r = fmod(x, 2.0);
    if (r == 0.0 || r == 1.0 || r == -1.0) return copysign(0.0, x);
    if (r == 0.5 || r == -1.5) return 1.0;
    if (r == -0.5 || r == 1.5) return -1.0;
    return sin(x * CCCC_PI);
}
static float cccc_sinpif(float x) {
    float r = fmodf(x, 2.0f);
    if (r == 0.0f || r == 1.0f || r == -1.0f) return copysignf(0.0f, x);
    if (r == 0.5f || r == -1.5f) return 1.0f;
    if (r == -0.5f || r == 1.5f) return -1.0f;
    return sinf(x * CCCC_PIf);
}
static double cccc_cospi(double x) {
    double r = fmod(x, 2.0);
    if (r < 0.0) r += 2.0;
    if (r == 0.0) return 1.0;
    if (r == 1.0) return -1.0;
    if (r == 0.5 || r == 1.5) return 0.0;
    return cos(x * CCCC_PI);
}
static float cccc_cospif(float x) {
    float r = fmodf(x, 2.0f);
    if (r < 0.0f) r += 2.0f;
    if (r == 0.0f) return 1.0f;
    if (r == 1.0f) return -1.0f;
    if (r == 0.5f || r == 1.5f) return 0.0f;
    return cosf(x * CCCC_PIf);
}
static double cccc_tanpi(double x) {
    if (fmod(x, 1.0) == 0.0) return copysign(0.0, x);
    return tan(x * CCCC_PI);
}
static float cccc_tanpif(float x) {
    if (fmodf(x, 1.0f) == 0.0f) return copysignf(0.0f, x);
    return tanf(x * CCCC_PIf);
}
#endif

// Long double pi-trig variants bridged at double precision (#491).
static double cccc_sinpil(double x)  { return cccc_sinpi(x); }
static double cccc_cospil(double x)  { return cccc_cospi(x); }
static double cccc_tanpil(double x)  { return cccc_tanpi(x); }

// ---- asinpi/acospi/atanpi/atan2pi family (C23) ----
// Absent everywhere; shimmed via division by pi. Precision loss from the
// division is negligible for inverse trig (unlike the sin/cos/tan case
// above, there are no exact poles/zeros that need preserving).
static double cccc_asinpi(double x)  { return asin(x) / CCCC_PI; }
static float  cccc_asinpif(float x)  { return asinf(x) / CCCC_PIf; }
static double cccc_asinpil(double x) { return cccc_asinpi(x); }

static double cccc_acospi(double x)  { return acos(x) / CCCC_PI; }
static float  cccc_acospif(float x)  { return acosf(x) / CCCC_PIf; }
static double cccc_acospil(double x) { return cccc_acospi(x); }

static double cccc_atanpi(double x)  { return atan(x) / CCCC_PI; }
static float  cccc_atanpif(float x)  { return atanf(x) / CCCC_PIf; }
static double cccc_atanpil(double x) { return cccc_atanpi(x); }

static double cccc_atan2pi(double y, double x)  { return atan2(y, x) / CCCC_PI; }
static float  cccc_atan2pif(float y, float x)   { return atan2f(y, x) / CCCC_PIf; }
static double cccc_atan2pil(double y, double x)  { return cccc_atan2pi(y, x); }

// ==================== C23 IEC 60559:2020 interchange functions (#774) ====
//
// Software implementations operating on raw IEEE-754 bit patterns, not FFI
// wrappers: Darwin's libm exports none of totalorder/totalordermag/
// fromfp*/ufromfp*/getpayload/setpayload*/llogb at all, and glibc only
// gained fmaximum/fminimum in 2.35 -- an FFI approach would need a
// platform/version matrix for no benefit over a direct implementation.
//
// `l` (long double) variants are double-precision shims, matching the rest
// of this file (#491): CCCC's guest long double is modeled as an 8-byte
// double, so bridging through a real host long double (80-bit on Linux
// x86_64, 128-bit on Linux aarch64) would be an ABI mismatch for no
// precision CCCC guest code can actually observe.

static uint64_t cccc_d2b(double x) { uint64_t u; memcpy(&u, &x, 8); return u; }
static double   cccc_b2d(uint64_t u) { double x; memcpy(&x, &u, 8); return x; }
static uint32_t cccc_f2b(float x) { uint32_t u; memcpy(&u, &x, 4); return u; }
static float    cccc_b2f(uint32_t u) { float x; memcpy(&x, &u, 4); return x; }

// ---- fmaximum/fminimum family ----
// Unlike fmax/fmin, these propagate NaN (either argument NaN => NaN
// result) and treat +0 as greater than -0 for equal-magnitude zero. The
// _num variants ignore a single NaN like fmax/fmin (NaN only if *both*
// are NaN); the _mag variants compare |x| vs |y|, falling back to the
// corresponding non-mag function on a magnitude tie.

#define CCCC_DEF_FMAXIMUM_FAMILY(SUF, T, ISNAN, SIGNBIT, FABS)              \
static T cccc_fmaximum##SUF(T x, T y) {                                     \
    if (ISNAN(x)) return x;                                                 \
    if (ISNAN(y)) return y;                                                 \
    if (x == y) return (SIGNBIT(x) && !SIGNBIT(y)) ? y : x;                 \
    return x > y ? x : y;                                                   \
}                                                                            \
static T cccc_fminimum##SUF(T x, T y) {                                     \
    if (ISNAN(x)) return x;                                                 \
    if (ISNAN(y)) return y;                                                 \
    if (x == y) return (SIGNBIT(x) && !SIGNBIT(y)) ? x : y;                 \
    return x < y ? x : y;                                                   \
}                                                                            \
static T cccc_fmaximum_num##SUF(T x, T y) {                                 \
    if (ISNAN(x) && ISNAN(y)) return x;                                     \
    if (ISNAN(x)) return y;                                                 \
    if (ISNAN(y)) return x;                                                 \
    return cccc_fmaximum##SUF(x, y);                                        \
}                                                                            \
static T cccc_fminimum_num##SUF(T x, T y) {                                 \
    if (ISNAN(x) && ISNAN(y)) return x;                                     \
    if (ISNAN(x)) return y;                                                 \
    if (ISNAN(y)) return x;                                                 \
    return cccc_fminimum##SUF(x, y);                                        \
}                                                                            \
static T cccc_fmaximum_mag##SUF(T x, T y) {                                 \
    if (ISNAN(x)) return x;                                                 \
    if (ISNAN(y)) return y;                                                 \
    T ax = FABS(x), ay = FABS(y);                                           \
    if (ax == ay) return cccc_fmaximum##SUF(x, y);                          \
    return ax > ay ? x : y;                                                 \
}                                                                            \
static T cccc_fminimum_mag##SUF(T x, T y) {                                 \
    if (ISNAN(x)) return x;                                                 \
    if (ISNAN(y)) return y;                                                 \
    T ax = FABS(x), ay = FABS(y);                                           \
    if (ax == ay) return cccc_fminimum##SUF(x, y);                          \
    return ax < ay ? x : y;                                                 \
}                                                                            \
static T cccc_fmaximum_mag_num##SUF(T x, T y) {                             \
    if (ISNAN(x) && ISNAN(y)) return x;                                     \
    if (ISNAN(x)) return y;                                                 \
    if (ISNAN(y)) return x;                                                 \
    T ax = FABS(x), ay = FABS(y);                                           \
    if (ax == ay) return cccc_fmaximum_num##SUF(x, y);                      \
    return ax > ay ? x : y;                                                 \
}                                                                            \
static T cccc_fminimum_mag_num##SUF(T x, T y) {                             \
    if (ISNAN(x) && ISNAN(y)) return x;                                     \
    if (ISNAN(x)) return y;                                                 \
    if (ISNAN(y)) return x;                                                 \
    T ax = FABS(x), ay = FABS(y);                                           \
    if (ax == ay) return cccc_fminimum_num##SUF(x, y);                      \
    return ax < ay ? x : y;                                                 \
}

CCCC_DEF_FMAXIMUM_FAMILY(, double, isnan, signbit, fabs)
CCCC_DEF_FMAXIMUM_FAMILY(f, float, isnan, signbit, fabsf)

static double cccc_fmaximuml(double x, double y) { return cccc_fmaximum(x, y); }
static double cccc_fminimuml(double x, double y) { return cccc_fminimum(x, y); }
static double cccc_fmaximum_numl(double x, double y) { return cccc_fmaximum_num(x, y); }
static double cccc_fminimum_numl(double x, double y) { return cccc_fminimum_num(x, y); }
static double cccc_fmaximum_magl(double x, double y) { return cccc_fmaximum_mag(x, y); }
static double cccc_fminimum_magl(double x, double y) { return cccc_fminimum_mag(x, y); }
static double cccc_fmaximum_mag_numl(double x, double y) { return cccc_fmaximum_mag_num(x, y); }
static double cccc_fminimum_mag_numl(double x, double y) { return cccc_fminimum_mag_num(x, y); }

// ---- totalorder/totalordermag ----
// IEEE-754-2019 totalOrder predicate via the standard "radix-sortable
// float" bit trick: XOR every bit with all-1s when the sign bit is set
// (reverses the ordering among negatives, so larger-magnitude negatives
// get smaller keys), or XOR just the sign bit when clear (shifts all
// positives above all negatives). Comparing the results as UNSIGNED
// integers then yields the total order, including NaN payloads ordered by
// raw bit pattern within each sign, matching the standard's requirement.
// (Comparison must be unsigned -- a signed comparison of the raw
// sign-bit-set keys, e.g. via a cast to int64_t, gets it backwards.)
static uint64_t cccc_order_key64(uint64_t u) {
    uint64_t mask = (u & 0x8000000000000000ULL) ? 0xFFFFFFFFFFFFFFFFULL : 0x8000000000000000ULL;
    return u ^ mask;
}
static uint32_t cccc_order_key32(uint32_t u) {
    uint32_t mask = (u & 0x80000000U) ? 0xFFFFFFFFU : 0x80000000U;
    return u ^ mask;
}

// Pointer parameters (matching glibc/ISO C): needed to observe a
// signaling NaN's exact bit pattern without an intervening FP operation
// quieting it -- unlike fmaximum/fminimum et al, totalorder is specified
// to work correctly even when x or y is a signaling NaN.
static long long cccc_totalorder(long long xp, long long yp) {
    double x = *(const double *)(intptr_t)xp, y = *(const double *)(intptr_t)yp;
    return cccc_order_key64(cccc_d2b(x)) <= cccc_order_key64(cccc_d2b(y));
}
static long long cccc_totalorderf(long long xp, long long yp) {
    float x = *(const float *)(intptr_t)xp, y = *(const float *)(intptr_t)yp;
    return cccc_order_key32(cccc_f2b(x)) <= cccc_order_key32(cccc_f2b(y));
}
static long long cccc_totalorderl(long long xp, long long yp) { return cccc_totalorder(xp, yp); }

static long long cccc_totalordermag(long long xp, long long yp) {
    double x = *(const double *)(intptr_t)xp, y = *(const double *)(intptr_t)yp;
    return cccc_order_key64(cccc_d2b(fabs(x))) <= cccc_order_key64(cccc_d2b(fabs(y)));
}
static long long cccc_totalordermagf(long long xp, long long yp) {
    float x = *(const float *)(intptr_t)xp, y = *(const float *)(intptr_t)yp;
    return cccc_order_key32(cccc_f2b(fabsf(x))) <= cccc_order_key32(cccc_f2b(fabsf(y)));
}
static long long cccc_totalordermagl(long long xp, long long yp) { return cccc_totalordermag(xp, yp); }

// ---- canonicalize ----
// IEEE 754 binary32/binary64 (CCCC's float/double) have no non-canonical
// encodings (unlike decimal floating types), so this is always a plain
// copy that reports success.
static long long cccc_canonicalize(long long cx, long long x) {
    *(double *)(intptr_t)cx = *(const double *)(intptr_t)x;
    return 0;
}
static long long cccc_canonicalizef(long long cx, long long x) {
    *(float *)(intptr_t)cx = *(const float *)(intptr_t)x;
    return 0;
}
static long long cccc_canonicalizel(long long cx, long long x) {
    return cccc_canonicalize(cx, x);
}

// ---- getpayload/setpayload/setpayload_sig ----
// The payload occupies all significand bits below the quiet/signaling
// bit (bit 51 for double, bit 22 for float). getpayload returns -1 for a
// non-NaN input. setpayload builds a quiet NaN (quiet bit set);
// setpayload_sig builds a signaling NaN (quiet bit clear) -- which
// requires a nonzero payload, since an all-zero significand with the
// quiet bit clear would be infinity, not a NaN.
#define CCCC_DBL_QUIET_BIT   0x0008000000000000ULL
#define CCCC_DBL_PAYLOAD_MAX 0x0007FFFFFFFFFFFFULL // 51 bits, excludes the quiet bit
#define CCCC_DBL_MANT_MASK   0x000FFFFFFFFFFFFFULL // 52 bits, full mantissa incl. quiet bit
#define CCCC_FLT_QUIET_BIT   0x00400000U
#define CCCC_FLT_PAYLOAD_MAX 0x003FFFFFU // 22 bits, excludes the quiet bit
#define CCCC_FLT_MANT_MASK   0x007FFFFFU // 23 bits, full mantissa incl. quiet bit

static double cccc_getpayload(long long xp) {
    double x = *(const double *)(intptr_t)xp;
    if (!isnan(x)) return -1.0;
    uint64_t bits = cccc_d2b(x) & CCCC_DBL_PAYLOAD_MAX;
    return (double)bits;
}
static float cccc_getpayloadf(long long xp) {
    float x = *(const float *)(intptr_t)xp;
    if (!isnan(x)) return -1.0f;
    uint32_t bits = cccc_f2b(x) & CCCC_FLT_PAYLOAD_MAX;
    return (float)bits;
}
static double cccc_getpayloadl(long long xp) { return cccc_getpayload(xp); }

static long long cccc_setpayload_impl(long long xp, double payload, int quiet) {
    // LIMITATION (#1079): on failure, this leaves *xp completely untouched
    // -- real glibc instead zeroes the destination ("if the payload was
    // not successfully installed, zero is stored in *cx", per the
    // documented behavior, matching what was measured directly against
    // real glibc x86_64 in the cccc-linux-amd64 container). Found via
    // test_math_c23_ieee.c: a failed setpayloadsig() call left ssig as its
    // still-signaling prior value under the VM, while native's real glibc
    // zeroed it -- a genuine VM-side spec-conformance bug, not
    // -c=native-specific, just never caught before since no existing test
    // inspects the destination after a failed call. Not fixed yet; should
    // zero *(double *)(intptr_t)xp on both early-return paths below before
    // returning 1.
    if (payload < 0.0 || payload != (double)(uint64_t)payload ||
        (uint64_t)payload > CCCC_DBL_PAYLOAD_MAX)
        return 1;
    if (!quiet && (uint64_t)payload == 0)
        return 1; // signaling NaN payload cannot be all-zero (would be Inf)
    uint64_t bits = 0x7FF0000000000000ULL | (uint64_t)payload;
    if (quiet) bits |= CCCC_DBL_QUIET_BIT;
    *(double *)(intptr_t)xp = cccc_b2d(bits);
    return 0;
}
static long long cccc_setpayload(long long xp, double payload) { return cccc_setpayload_impl(xp, payload, 1); }
static long long cccc_setpayloadsig(long long xp, double payload) { return cccc_setpayload_impl(xp, payload, 0); }

static long long cccc_setpayloadf_impl(long long xp, float payload, int quiet) {
    // LIMITATION (#1079): same untouched-on-failure gap as
    // cccc_setpayload_impl above -- see its own comment.
    if (payload < 0.0f || payload != (float)(uint32_t)payload ||
        (uint32_t)payload > CCCC_FLT_PAYLOAD_MAX)
        return 1;
    if (!quiet && (uint32_t)payload == 0)
        return 1;
    uint32_t bits = 0x7F800000U | (uint32_t)payload;
    if (quiet) bits |= CCCC_FLT_QUIET_BIT;
    *(float *)(intptr_t)xp = cccc_b2f(bits);
    return 0;
}
static long long cccc_setpayloadf(long long xp, float payload) { return cccc_setpayloadf_impl(xp, payload, 1); }
static long long cccc_setpayloadsigf(long long xp, float payload) { return cccc_setpayloadf_impl(xp, payload, 0); }

static long long cccc_setpayloadl(long long xp, double payload) { return cccc_setpayload(xp, payload); }
static long long cccc_setpayloadsigl(long long xp, double payload) { return cccc_setpayloadsig(xp, payload); }

// ---- llogb ----
// Like ilogb but returns long. FP_ILOGB0/FP_ILOGBNAN (from <math.h>) are
// reused for the 0/NaN special cases via ilogb() itself, since llogb's
// special-case values are the same, just widened to `long`.
static long cccc_llogb(double x) { return (long)ilogb(x); }
static long cccc_llogbf(float x) { return (long)ilogbf(x); }
static long cccc_llogbl(double x) { return cccc_llogb(x); }

// ---- fromfp/ufromfp family ----
// Rounds x to an integer per rounding direction `rnd`, that fits in a
// `width`-bit signed (fromfp/fromfpx) or unsigned (ufromfp/ufromfpx)
// integer, returned as intmax_t/uintmax_t -- NOT the source floating
// type. These generalize lround/llround with a configurable rounding
// direction and width (matching glibc's real signature:
// intmax_t fromfp(double, int, unsigned int)), not "round to the nearest
// integer-valued float". Returns 0 (raising FE_INVALID) if the rounded
// value doesn't fit. The 'x' variants additionally raise FE_INEXACT if
// the rounded result differs from the input.
static double cccc_round_by_direction(double x, int rnd) {
    switch (rnd) {
    case CCCC_FP_INT_UPWARD:              return ceil(x);
    case CCCC_FP_INT_DOWNWARD:            return floor(x);
    case CCCC_FP_INT_TOWARDZERO:          return trunc(x);
    case CCCC_FP_INT_TONEARESTFROMZERO:   return round(x);
    case CCCC_FP_INT_TONEAREST: default:  return rint(x);
    }
}

// Returns the rounded integer value bit-reinterpreted into a long long --
// valid either as intmax_t (fromfp/fromfpx) or uintmax_t (ufromfp/
// ufromfpx) depending on which the caller declared, since both are 64-bit
// and the guest marshals all non-float/double returns through the same
// register regardless of signedness.
static long long cccc_fromfp_impl(double x, long long rnd, long long width, int is_unsigned, int extended) {
    if (isnan(x) || isinf(x) || width == 0) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    double r = cccc_round_by_direction(x, (int)rnd);
    double lo, hi; // inclusive range representable in `width` bits
    if (is_unsigned) {
        lo = 0.0;
        hi = (width >= 64) ? 18446744073709551615.0 : (ldexp(1.0, (int)width) - 1.0);
    } else {
        hi = (width >= 64) ? 9223372036854775807.0 : (ldexp(1.0, (int)width - 1) - 1.0);
        lo = (width >= 64) ? -9223372036854775808.0 : -ldexp(1.0, (int)width - 1);
    }
    if (r < lo || r > hi) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    if (extended && r != x)
        feraiseexcept(FE_INEXACT);
    return is_unsigned ? (long long)(uint64_t)r : (long long)(int64_t)r;
}

static long long cccc_fromfp(double x, long long rnd, long long width)   { return cccc_fromfp_impl(x, rnd, width, 0, 0); }
static long long cccc_ufromfp(double x, long long rnd, long long width)  { return cccc_fromfp_impl(x, rnd, width, 1, 0); }
static long long cccc_fromfpx(double x, long long rnd, long long width)  { return cccc_fromfp_impl(x, rnd, width, 0, 1); }
static long long cccc_ufromfpx(double x, long long rnd, long long width) { return cccc_fromfp_impl(x, rnd, width, 1, 1); }

static long long cccc_fromfpf(float x, long long rnd, long long width)   { return cccc_fromfp_impl((double)x, rnd, width, 0, 0); }
static long long cccc_ufromfpf(float x, long long rnd, long long width)  { return cccc_fromfp_impl((double)x, rnd, width, 1, 0); }
static long long cccc_fromfpxf(float x, long long rnd, long long width)  { return cccc_fromfp_impl((double)x, rnd, width, 0, 1); }
static long long cccc_ufromfpxf(float x, long long rnd, long long width) { return cccc_fromfp_impl((double)x, rnd, width, 1, 1); }

static long long cccc_fromfpl(double x, long long rnd, long long width)   { return cccc_fromfp(x, rnd, width); }
static long long cccc_ufromfpl(double x, long long rnd, long long width)  { return cccc_ufromfp(x, rnd, width); }
static long long cccc_fromfpxl(double x, long long rnd, long long width)  { return cccc_fromfpx(x, rnd, width); }
static long long cccc_ufromfpxl(double x, long long rnd, long long width) { return cccc_ufromfpx(x, rnd, width); }

// ---- issignaling/iseqsig (backing __cccc_issignaling_*/__cccc_iseqsig_*
// macros dispatched from <math.h>) ----
// issignaling tests the quiet bit directly on the raw bit pattern rather
// than going through isnan()/arithmetic, since either would quiet a
// signaling NaN before it could be observed.
static long long cccc_issignaling_d(double x) {
    uint64_t u = cccc_d2b(x);
    return ((u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && // NaN or Inf
           (u & CCCC_DBL_PAYLOAD_MAX) != 0 &&                        // NaN, not Inf
           !(u & CCCC_DBL_QUIET_BIT);                                // quiet bit clear
}
static long long cccc_issignaling_f(float x) {
    uint32_t u = cccc_f2b(x);
    return ((u & 0x7F800000U) == 0x7F800000U) &&
           (u & CCCC_FLT_PAYLOAD_MAX) != 0 &&
           !(u & CCCC_FLT_QUIET_BIT);
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
#define CCCC_FP_INFINITE 1
#define CCCC_FP_NAN 2
#define CCCC_FP_NORMAL 3
#define CCCC_FP_SUBNORMAL 4
#define CCCC_FP_ZERO 5

// NB: uses CCCC_D/FLT_MANT_MASK (the full mantissa, including the quiet
// bit) to test "is the mantissa nonzero", NOT CCCC_D/FLT_PAYLOAD_MAX (which
// deliberately excludes the quiet bit, for getpayload/setpayload). A plain
// quiet NaN with zero payload -- e.g. 0.0/0.0, whose mantissa is just the
// quiet bit itself -- has a nonzero full mantissa but a zero payload, so
// using PAYLOAD_MAX here would misclassify it as infinity.
static long long cccc_isnan_d(double x) {
    uint64_t u = cccc_d2b(x);
    return (u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL &&
           (u & CCCC_DBL_MANT_MASK) != 0;
}
static long long cccc_isnan_f(float x) {
    uint32_t u = cccc_f2b(x);
    return (u & 0x7F800000U) == 0x7F800000U && (u & CCCC_FLT_MANT_MASK) != 0;
}

static long long cccc_isinf_d(double x) {
    uint64_t u = cccc_d2b(x);
    return (u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL &&
           (u & CCCC_DBL_MANT_MASK) == 0;
}
static long long cccc_isinf_f(float x) {
    uint32_t u = cccc_f2b(x);
    return (u & 0x7F800000U) == 0x7F800000U && (u & CCCC_FLT_MANT_MASK) == 0;
}

// signbit tests the raw sign bit directly -- correct for -0.0 (bit set,
// unlike `x < 0`) and for NaN (whatever its actual sign bit is, unlike
// `x < 0`'s always-false result since every NaN comparison is false).
static long long cccc_signbit_d(double x) { return cccc_d2b(x) >> 63; }
static long long cccc_signbit_f(float x) { return cccc_f2b(x) >> 31; }

static long long cccc_fpclassify_d(double x) {
    uint64_t u = cccc_d2b(x);
    uint64_t exp = u & 0x7FF0000000000000ULL;
    uint64_t mant = u & CCCC_DBL_MANT_MASK; // full mantissa -- see isnan/isinf note above
    if (exp == 0x7FF0000000000000ULL) return mant ? CCCC_FP_NAN : CCCC_FP_INFINITE;
    if (exp == 0) return mant ? CCCC_FP_SUBNORMAL : CCCC_FP_ZERO;
    return CCCC_FP_NORMAL;
}
static long long cccc_fpclassify_f(float x) {
    uint32_t u = cccc_f2b(x);
    uint32_t exp = u & 0x7F800000U;
    uint32_t mant = u & CCCC_FLT_MANT_MASK; // full mantissa -- see isnan/isinf note above
    if (exp == 0x7F800000U) return mant ? CCCC_FP_NAN : CCCC_FP_INFINITE;
    if (exp == 0) return mant ? CCCC_FP_SUBNORMAL : CCCC_FP_ZERO;
    return CCCC_FP_NORMAL;
}

// Register all math.h functions
void register_math_functions(VirtualMachine *vm) {
    // Basic operations
    cc_register_cfunc_ex(vm, "fabs", (void*)fabs, 1, 1, 0b1);
    cc_register_cfunc(vm, "fabsf", (void*)fabsf, 1, 2);
    cc_register_cfunc_ex(vm, "fabsl", (void*)fabs, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "fmod", (void*)fmod, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmodf", (void*)fmodf, 2, 2, 0);    cc_register_cfunc_ex(vm, "fmodl", (void*)fmod, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remainder", (void*)remainder, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remainderf", (void*)remainderf, 2, 2, 0);    cc_register_cfunc_ex(vm, "remainderl", (void*)remainder, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remquo", (void*)remquo, 3, 1, 0b11);  // double, double, int*
    cc_register_cfunc_ex(vm, "remquof", (void*)remquof, 3, 2, 0);    cc_register_cfunc_ex(vm, "remquol", (void*)remquo, 3, 1, 0b11);
    cc_register_cfunc_ex(vm, "fma", (void*)fma, 3, 1, 0b111);  // double, double, double
    cc_register_cfunc_ex(vm, "fmaf", (void*)fmaf, 3, 2, 0);    cc_register_cfunc_ex(vm, "fmal", (void*)fma, 3, 1, 0b111);
    cc_register_cfunc_ex(vm, "fmax", (void*)fmax, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmaxf", (void*)fmaxf, 2, 2, 0);    cc_register_cfunc_ex(vm, "fmaxl", (void*)fmax, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmin", (void*)fmin, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fminf", (void*)fminf, 2, 2, 0);    cc_register_cfunc_ex(vm, "fminl", (void*)fmin, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fdim", (void*)fdim, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fdimf", (void*)fdimf, 2, 2, 0);    cc_register_cfunc_ex(vm, "fdiml", (void*)fdim, 2, 1, 0b11);
    cc_register_cfunc(vm, "nan", (void*)nan, 1, 1);
    cc_register_cfunc(vm, "nanf", (void*)nanf, 1, 2);    cc_register_cfunc(vm, "nanl", (void*)nan, 1, 1);

    // Exponential/logarithmic - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "exp", (void*)exp, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expf", (void*)expf, 1, 2, 0);    cc_register_cfunc_ex(vm, "expl", (void*)exp, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "exp2", (void*)exp2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "exp2f", (void*)exp2f, 1, 2, 0);    cc_register_cfunc_ex(vm, "exp2l", (void*)exp2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expm1", (void*)expm1, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expm1f", (void*)expm1f, 1, 2, 0);    cc_register_cfunc_ex(vm, "expm1l", (void*)expm1, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log", (void*)log, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "logf", (void*)logf, 1, 2, 0);    cc_register_cfunc_ex(vm, "logl", (void*)log, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log10", (void*)log10, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log10f", (void*)log10f, 1, 2, 0);    cc_register_cfunc_ex(vm, "log10l", (void*)log10, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log2", (void*)log2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log2f", (void*)log2f, 1, 2, 0);    cc_register_cfunc_ex(vm, "log2l", (void*)log2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log1p", (void*)log1p, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log1pf", (void*)log1pf, 1, 2, 0);    cc_register_cfunc_ex(vm, "log1pl", (void*)log1p, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "pow", (void*)pow, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "powf", (void*)powf, 2, 2, 0);    cc_register_cfunc_ex(vm, "powl", (void*)pow, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "sqrt", (void*)sqrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sqrtf", (void*)sqrtf, 1, 2, 0);    cc_register_cfunc_ex(vm, "sqrtl", (void*)sqrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cbrt", (void*)cbrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cbrtf", (void*)cbrtf, 1, 2, 0);    cc_register_cfunc_ex(vm, "cbrtl", (void*)cbrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "hypot", (void*)hypot, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "hypotf", (void*)hypotf, 2, 2, 0);    cc_register_cfunc_ex(vm, "hypotl", (void*)hypot, 2, 1, 0b11);

    // Trigonometric - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "sin", (void*)sin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sinf", (void*)sinf, 1, 2, 0);    cc_register_cfunc_ex(vm, "sinl", (void*)sin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cos", (void*)cos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cosf", (void*)cosf, 1, 2, 0);    cc_register_cfunc_ex(vm, "cosl", (void*)cos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tan", (void*)tan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanf", (void*)tanf, 1, 2, 0);    cc_register_cfunc_ex(vm, "tanl", (void*)tan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asin", (void*)asin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinf", (void*)asinf, 1, 2, 0);    cc_register_cfunc_ex(vm, "asinl", (void*)asin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acos", (void*)acos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acosf", (void*)acosf, 1, 2, 0);    cc_register_cfunc_ex(vm, "acosl", (void*)acos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atan", (void*)atan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanf", (void*)atanf, 1, 2, 0);    cc_register_cfunc_ex(vm, "atanl", (void*)atan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atan2", (void*)atan2, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "atan2f", (void*)atan2f, 2, 2, 0);    cc_register_cfunc_ex(vm, "atan2l", (void*)atan2, 2, 1, 0b11);

    // Hyperbolic - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "sinh", (void*)sinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sinhf", (void*)sinhf, 1, 2, 0);    cc_register_cfunc_ex(vm, "sinhl", (void*)sinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cosh", (void*)cosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "coshf", (void*)coshf, 1, 2, 0);    cc_register_cfunc_ex(vm, "coshl", (void*)cosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanh", (void*)tanh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanhf", (void*)tanhf, 1, 2, 0);    cc_register_cfunc_ex(vm, "tanhl", (void*)tanh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinh", (void*)asinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinhf", (void*)asinhf, 1, 2, 0);    cc_register_cfunc_ex(vm, "asinhl", (void*)asinh, 1, 1, 0b1);
    // acosh/atanh (#777): declared in include/math.h but never registered.
    cc_register_cfunc_ex(vm, "acosh", (void*)acosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acoshf", (void*)acoshf, 1, 2, 0);    cc_register_cfunc_ex(vm, "acoshl", (void*)acosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanh", (void*)atanh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanhf", (void*)atanhf, 1, 2, 0);    cc_register_cfunc_ex(vm, "atanhl", (void*)atanh, 1, 1, 0b1);

    // Special functions - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "erf", (void*)erf, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erff", (void*)erff, 1, 2, 0);    cc_register_cfunc_ex(vm, "erfl", (void*)erf, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erfc", (void*)erfc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erfcf", (void*)erfcf, 1, 2, 0);    cc_register_cfunc_ex(vm, "erfcl", (void*)erfc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tgamma", (void*)tgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tgammaf", (void*)tgammaf, 1, 2, 0);    cc_register_cfunc_ex(vm, "tgammal", (void*)tgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lgamma", (void*)lgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lgammaf", (void*)lgammaf, 1, 2, 0);    cc_register_cfunc_ex(vm, "lgammal", (void*)lgamma, 1, 1, 0b1);

    // Rounding - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "ceil", (void*)ceil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "ceilf", (void*)ceilf, 1, 2, 0);    cc_register_cfunc_ex(vm, "ceill", (void*)ceil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "floor", (void*)floor, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "floorf", (void*)floorf, 1, 2, 0);    cc_register_cfunc_ex(vm, "floorl", (void*)floor, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "trunc", (void*)trunc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "truncf", (void*)truncf, 1, 2, 0);    cc_register_cfunc_ex(vm, "truncl", (void*)trunc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "round", (void*)round, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "roundf", (void*)roundf, 1, 2, 0);    cc_register_cfunc_ex(vm, "roundl", (void*)round, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lround", (void*)lround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lroundf", (void*)lroundf, 1, 0, 0);
    cc_register_cfunc_ex(vm, "lroundl", (void*)lround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llround", (void*)llround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llroundf", (void*)llroundf, 1, 0, 0);
    cc_register_cfunc_ex(vm, "llroundl", (void*)llround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "nearbyint", (void*)nearbyint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "nearbyintf", (void*)nearbyintf, 1, 2, 0);    cc_register_cfunc_ex(vm, "nearbyintl", (void*)nearbyint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "rint", (void*)rint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "rintf", (void*)rintf, 1, 2, 0);    cc_register_cfunc_ex(vm, "rintl", (void*)rint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lrint", (void*)lrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lrintf", (void*)lrintf, 1, 0, 0);
    cc_register_cfunc_ex(vm, "lrintl", (void*)lrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llrint", (void*)llrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llrintf", (void*)llrintf, 1, 0, 0);
    cc_register_cfunc_ex(vm, "llrintl", (void*)llrint, 1, 0, 0b1);

    // Manipulation
    cc_register_cfunc_ex(vm, "frexp", (void*)frexp, 2, 1, 0b1);
    cc_register_cfunc(vm, "frexpf", (void*)frexpf, 2, 2);
    cc_register_cfunc_ex(vm, "frexpl", (void*)frexp, 2, 1, 0b1);
    cc_register_cfunc_ex(vm, "ldexp", (void*)ldexp, 2, 1, 0b01);  // double, int
    cc_register_cfunc_ex(vm, "ldexpf", (void*)ldexpf, 2, 2, 0);    cc_register_cfunc_ex(vm, "ldexpl", (void*)ldexp, 2, 1, 0b01);
    cc_register_cfunc_ex(vm, "modf", (void*)modf, 2, 1, 0b1);  // double, double*
    cc_register_cfunc_ex(vm, "modff", (void*)modff, 2, 2, 0);    cc_register_cfunc_ex(vm, "modfl", (void*)modf, 2, 1, 0b1);
    cc_register_cfunc_ex(vm, "scalbn", (void*)scalbn, 2, 1, 0b01);  // double, int
    cc_register_cfunc_ex(vm, "scalbnf", (void*)scalbnf, 2, 2, 0);    cc_register_cfunc_ex(vm, "scalbnl", (void*)scalbn, 2, 1, 0b01);
    cc_register_cfunc_ex(vm, "scalbln", (void*)scalbln, 2, 1, 0b01);  // double, long
    cc_register_cfunc_ex(vm, "scalblnf", (void*)scalblnf, 2, 2, 0);    cc_register_cfunc_ex(vm, "scalblnl", (void*)scalbln, 2, 1, 0b01);
    cc_register_cfunc_ex(vm, "ilogb", (void*)ilogb, 1, 0, 0b1);
    cc_register_cfunc(vm, "ilogbf", (void*)ilogbf, 1, 0);
    cc_register_cfunc_ex(vm, "ilogbl", (void*)ilogb, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "logb", (void*)logb, 1, 1, 0b1);
    cc_register_cfunc(vm, "logbf", (void*)logbf, 1, 2);    cc_register_cfunc_ex(vm, "logbl", (void*)logb, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "nextafter", (void*)nextafter, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "nextafterf", (void*)nextafterf, 2, 2, 0);    cc_register_cfunc_ex(vm, "nextafterl", (void*)nextafter, 2, 1, 0b11);
    // nexttoward(double, long double) / nexttowardf(float, long double): the 2nd arg is
    // long double which is 128-bit on Linux/aarch64.  The VM models long double as 8 bytes,
    // so we redirect to nextafter/nextafterf (same return type, same double-ABI) to avoid
    // the 128-bit host ABI mismatch (#491).  nexttowardl already redirects to nextafter below.
    // nexttowardf uses a wrapper (cccc_nexttowardf) instead of nextafterf directly: CCCC
    // inserts an implicit cast on the long double arg (→ double), so CALLF sees arg1 as
    // double.  Passing a double to nextafterf(float,float) via libffi gives the wrong low
    // 32-bit interpretation on arm64 (#498).  The wrapper accepts (float, double) and casts
    // back to float before forwarding.
    cc_register_cfunc_ex(vm, "nexttoward",  (void*)nextafter,         2, 1, 0b11);
    cc_register_cfunc_ex(vm, "nexttowardf", (void*)cccc_nexttowardf,  2, 2, 0b10);    cc_register_cfunc_ex(vm, "nexttowardl", (void*)nextafter, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "copysign", (void*)copysign, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "copysignf", (void*)copysignf, 2, 2, 0);    cc_register_cfunc_ex(vm, "copysignl", (void*)copysign, 2, 1, 0b11);

    // C23 exp10 family - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "exp10", (void*)cccc_exp10, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "exp10f", (void*)cccc_exp10f, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "exp10l", (void*)cccc_exp10l, 1, 1, 0b1);

    // C23 sinpi/cospi/tanpi family - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "sinpi", (void*)cccc_sinpi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sinpif", (void*)cccc_sinpif, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "sinpil", (void*)cccc_sinpil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cospi", (void*)cccc_cospi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cospif", (void*)cccc_cospif, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "cospil", (void*)cccc_cospil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanpi", (void*)cccc_tanpi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanpif", (void*)cccc_tanpif, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "tanpil", (void*)cccc_tanpil, 1, 1, 0b1);

    // C23 asinpi/acospi/atanpi/atan2pi family
    cc_register_cfunc_ex(vm, "asinpi", (void*)cccc_asinpi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinpif", (void*)cccc_asinpif, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "asinpil", (void*)cccc_asinpil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acospi", (void*)cccc_acospi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acospif", (void*)cccc_acospif, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "acospil", (void*)cccc_acospil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanpi", (void*)cccc_atanpi, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanpif", (void*)cccc_atanpif, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "atanpil", (void*)cccc_atanpil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atan2pi", (void*)cccc_atan2pi, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "atan2pif", (void*)cccc_atan2pif, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "atan2pil", (void*)cccc_atan2pil, 2, 1, 0b11);

    // C23 IEC 60559:2020 interchange functions (#774)
#define CCCC_REG_FMAXIMUM_FAMILY(NAME)                                                      \
    cc_register_cfunc_ex(vm, #NAME, (void*)cccc_##NAME, 2, 1, 0b11);                         \
    cc_register_cfunc_ex(vm, #NAME "f", (void*)cccc_##NAME##f, 2, 2, 0b11);                  \
    cc_register_cfunc_ex(vm, #NAME "l", (void*)cccc_##NAME##l, 2, 1, 0b11);
    CCCC_REG_FMAXIMUM_FAMILY(fmaximum)
    CCCC_REG_FMAXIMUM_FAMILY(fminimum)
    CCCC_REG_FMAXIMUM_FAMILY(fmaximum_num)
    CCCC_REG_FMAXIMUM_FAMILY(fminimum_num)
    CCCC_REG_FMAXIMUM_FAMILY(fmaximum_mag)
    CCCC_REG_FMAXIMUM_FAMILY(fminimum_mag)
    CCCC_REG_FMAXIMUM_FAMILY(fmaximum_mag_num)
    CCCC_REG_FMAXIMUM_FAMILY(fminimum_mag_num)
#undef CCCC_REG_FMAXIMUM_FAMILY

    cc_register_cfunc_ex(vm, "totalorder", (void*)cccc_totalorder, 2, 0, 0b00);      // const double*, const double*
    cc_register_cfunc_ex(vm, "totalorderf", (void*)cccc_totalorderf, 2, 0, 0b00);    // const float*, const float*
    cc_register_cfunc_ex(vm, "totalorderl", (void*)cccc_totalorderl, 2, 0, 0b00);
    cc_register_cfunc_ex(vm, "totalordermag", (void*)cccc_totalordermag, 2, 0, 0b00);
    cc_register_cfunc_ex(vm, "totalordermagf", (void*)cccc_totalordermagf, 2, 0, 0b00);
    cc_register_cfunc_ex(vm, "totalordermagl", (void*)cccc_totalordermagl, 2, 0, 0b00);

    cc_register_cfunc_ex(vm, "canonicalize", (void*)cccc_canonicalize, 2, 0, 0b00);   // double*, const double*
    cc_register_cfunc_ex(vm, "canonicalizef", (void*)cccc_canonicalizef, 2, 0, 0b00); // float*, const float*
    cc_register_cfunc_ex(vm, "canonicalizel", (void*)cccc_canonicalizel, 2, 0, 0b00);

    cc_register_cfunc_ex(vm, "getpayload", (void*)cccc_getpayload, 1, 1, 0);    // const double* -> double
    cc_register_cfunc_ex(vm, "getpayloadf", (void*)cccc_getpayloadf, 1, 2, 0);  // const float* -> float
    cc_register_cfunc_ex(vm, "getpayloadl", (void*)cccc_getpayloadl, 1, 1, 0);

    cc_register_cfunc_ex(vm, "setpayload", (void*)cccc_setpayload, 2, 0, 0b10);       // double*, double
    cc_register_cfunc_ex(vm, "setpayloadf", (void*)cccc_setpayloadf, 2, 0, 0b10);     // float*, float
    cc_register_cfunc_ex(vm, "setpayloadl", (void*)cccc_setpayloadl, 2, 0, 0b10);
    cc_register_cfunc_ex(vm, "setpayloadsig", (void*)cccc_setpayloadsig, 2, 0, 0b10);
    cc_register_cfunc_ex(vm, "setpayloadsigf", (void*)cccc_setpayloadsigf, 2, 0, 0b10);
    cc_register_cfunc_ex(vm, "setpayloadsigl", (void*)cccc_setpayloadsigl, 2, 0, 0b10);

    cc_register_cfunc_ex(vm, "llogb", (void*)cccc_llogb, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llogbf", (void*)cccc_llogbf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llogbl", (void*)cccc_llogbl, 1, 0, 0b1);

    // fromfp/ufromfp/fromfpx/ufromfpx return intmax_t/uintmax_t (long
    // long, not double/float) -- only arg0 (x) is FP-class, so ret=0 with
    // mask 0b1 for all three width variants.
#define CCCC_REG_FROMFP_FAMILY(NAME)                                                         \
    cc_register_cfunc_ex(vm, #NAME, (void*)cccc_##NAME, 3, 0, 0b1);                          \
    cc_register_cfunc_ex(vm, #NAME "f", (void*)cccc_##NAME##f, 3, 0, 0b1);                   \
    cc_register_cfunc_ex(vm, #NAME "l", (void*)cccc_##NAME##l, 3, 0, 0b1);
    CCCC_REG_FROMFP_FAMILY(fromfp)
    CCCC_REG_FROMFP_FAMILY(ufromfp)
    CCCC_REG_FROMFP_FAMILY(fromfpx)
    CCCC_REG_FROMFP_FAMILY(ufromfpx)
#undef CCCC_REG_FROMFP_FAMILY

    cc_register_cfunc_ex(vm, "__cccc_issignaling_f", (void*)cccc_issignaling_f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_issignaling_d", (void*)cccc_issignaling_d, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_iseqsig_f", (void*)cccc_iseqsig_f, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "__cccc_iseqsig_d", (void*)cccc_iseqsig_d, 2, 0, 0b11);

    // Classification macro backing functions (#778)
    cc_register_cfunc_ex(vm, "__cccc_isnan_f", (void*)cccc_isnan_f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_isnan_d", (void*)cccc_isnan_d, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_isinf_f", (void*)cccc_isinf_f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_isinf_d", (void*)cccc_isinf_d, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_signbit_f", (void*)cccc_signbit_f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_signbit_d", (void*)cccc_signbit_d, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_fpclassify_f", (void*)cccc_fpclassify_f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "__cccc_fpclassify_d", (void*)cccc_fpclassify_d, 1, 0, 0b1);
}
