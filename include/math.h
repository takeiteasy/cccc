/* math.h - math functions for CCCC C compiler */

#ifndef __MATH_H
#define __MATH_H

#include "stddef.h"
#include "float.h"
#include "limits.h"
#include "stdint.h"

/* Floating-point constants */
#define HUGE_VAL (__builtin_huge_val())
#ifndef HUGE_VALF
#define HUGE_VALF ((float)HUGE_VAL)
#endif
#ifndef HUGE_VALL
#define HUGE_VALL ((long double)HUGE_VAL)
#endif
#define INFINITY (__builtin_inf())
#define NAN (__builtin_nan(""))

/* fpclassify values */
#define FP_INFINITE 1
#define FP_NAN 2
#define FP_NORMAL 3
#define FP_SUBNORMAL 4
#define FP_ZERO 5

/* ilogb special values */
#define FP_ILOGB0 INT_MIN
#define FP_ILOGBNAN INT_MAX

/* math error macros */
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2

/* Fast math availability macros (no-op placeholders) */
#define FP_FAST_FMA 1
#define FP_FAST_FMAF 1
#define FP_FAST_FMAL 1

#define isgreater(x, y) ((x) > (y))
#define isgreaterequal(x, y) ((x) >= (y))
#define isless(x, y) ((x) < (y))
#define islessequal(x, y) ((x) <= (y))
#define islessgreater(x, y) (((x) < (y)) || ((x) > (y)))
#define isunordered(x, y) (((x) != (x)) || ((y) != (y)))

/* isnan/isinf/signbit/fpclassify (#778): real bit-pattern functions in
 * src/stdlib/math.c, dispatched by argument type like the issignaling/
 * iseqsig macros below (same _Generic pattern as include/tgmath.h). These
 * used to be formulas referencing a bare isnan/isinf that was never
 * defined anywhere, so isfinite/isnormal/fpclassify failed to compile;
 * signbit(x) ((x) < 0) compiled but was wrong for -0.0 and NaN. */
int __cccc_isnan_f(float);
int __cccc_isnan_d(double);
#define isnan(x) _Generic((x), float: __cccc_isnan_f, default: __cccc_isnan_d)(x)

int __cccc_isinf_f(float);
int __cccc_isinf_d(double);
#define isinf(x) _Generic((x), float: __cccc_isinf_f, default: __cccc_isinf_d)(x)

int __cccc_signbit_f(float);
int __cccc_signbit_d(double);
#define signbit(x) _Generic((x), float: __cccc_signbit_f, default: __cccc_signbit_d)(x)

int __cccc_fpclassify_f(float);
int __cccc_fpclassify_d(double);
#define fpclassify(x) _Generic((x), float: __cccc_fpclassify_f, default: __cccc_fpclassify_d)(x)

#define isfinite(x) (!(isnan(x) || isinf(x)))
#define isnormal(x) (fpclassify(x) == FP_NORMAL)

/* Basic arithmetic */
double fabs(double);
float fabsf(float);
long double fabsl(long double);

double fmod(double, double);
float fmodf(float, float);
long double fmodl(long double, long double);

double remainder(double, double);
float remainderf(float, float);
long double remainderl(long double, long double);

int remquo(double, double, int *);
int remquof(float, float, int *);
int remquol(long double, long double, int *);

double fma(double, double, double);
float fmaf(float, float, float);
long double fmal(long double, long double, long double);

/* min/max/fdim */
double fmax(double, double);
float fmaxf(float, float);
long double fmaxl(long double, long double);

double fmin(double, double);
float fminf(float, float);
long double fminl(long double, long double);

double fdim(double, double);
float fdimf(float, float);
long double fdiml(long double, long double);

/* NaN creation */
double nan(const char *);
float nanf(const char *);
long double nanl(const char *);

/* Exponentials and logarithms */
double exp(double);
float expf(float);
long double expl(long double);

double exp2(double);
float exp2f(float);
long double exp2l(long double);

double expm1(double);
float expm1f(float);
long double expm1l(long double);

double log(double);
float logf(float);
long double logl(long double);

double log10(double);
float log10f(float);
long double log10l(long double);

double log2(double);
float log2f(float);
long double log2l(long double);

double log1p(double);
float log1pf(float);
long double log1pl(long double);

double pow(double, double);
float powf(float, float);
long double powl(long double, long double);

double sqrt(double);
float sqrtf(float);
long double sqrtl(long double);

double cbrt(double);
float cbrtf(float);
long double cbrtl(long double);

double hypot(double, double);
float hypotf(float, float);
long double hypotl(long double, long double);

/* Trigonometric functions */
double sin(double);
float sinf(float);
long double sinl(long double);

double cos(double);
float cosf(float);
long double cosl(long double);

double tan(double);
float tanf(float);
long double tanl(long double);

double asin(double);
float asinf(float);
long double asinl(long double);

double acos(double);
float acosf(float);
long double acosl(long double);

double atan(double);
float atanf(float);
long double atanl(long double);

double atan2(double, double);
float atan2f(float, float);
long double atan2l(long double, long double);

/* exp10 (C23) */
double exp10(double);
float exp10f(float);
long double exp10l(long double);

/* sinpi/cospi/tanpi (C23) */
double sinpi(double);
float sinpif(float);
long double sinpil(long double);

double cospi(double);
float cospif(float);
long double cospil(long double);

double tanpi(double);
float tanpif(float);
long double tanpil(long double);

/* asinpi/acospi/atanpi/atan2pi (C23) */
double asinpi(double);
float asinpif(float);
long double asinpil(long double);

double acospi(double);
float acospif(float);
long double acospil(long double);

double atanpi(double);
float atanpif(float);
long double atanpil(long double);

double atan2pi(double, double);
float atan2pif(float, float);
long double atan2pil(long double, long double);

/* Hyperbolic */
double sinh(double);
float sinhf(float);
long double sinhl(long double);

double cosh(double);
float coshf(float);
long double coshl(long double);

double tanh(double);
float tanhf(float);
long double tanhl(long double);

double asinh(double);
float asinhf(float);
long double asinhl(long double);

double acosh(double);
float acoshf(float);
long double acoshl(long double);

double atanh(double);
float atanhf(float);
long double atanhl(long double);

/* Error and gamma */
double erf(double);
float erff(float);
long double erfl(long double);

double erfc(double);
float erfcf(float);
long double erfcl(long double);

double tgamma(double);
float tgammaf(float);
long double tgammal(long double);

double lgamma(double);
float lgammaf(float);
long double lgammal(long double);

/* Rounding and remainder */
double ceil(double);
float ceilf(float);
long double ceill(long double);

double floor(double);
float floorf(float);
long double floorl(long double);

double trunc(double);
float truncf(float);
long double truncl(long double);

double round(double);
float roundf(float);
long double roundl(long double);

long lround(double);
long lroundf(float);
long lroundl(long double);

long long llround(double);
long long llroundf(float);
long long llroundl(long double);

double nearbyint(double);
float nearbyintf(float);
long double nearbyintl(long double);

double rint(double);
float rintf(float);
long double rintl(long double);

long lrint(double);
long lrintf(float);
long lrintl(long double);

long long llrint(double);
long long llrintf(float);
long long llrintl(long double);

/* Frex/ldexp/modf */
double frexp(double, int *);
float frexpf(float, int *);
long double frexpl(long double, int *);

double ldexp(double, int);
float ldexpf(float, int);
long double ldexpl(long double, int);

double modf(double, double *);
float modff(float, float *);
long double modfl(long double, long double *);

/* scalbn/scalbln */
double scalbn(double, int);
float scalbnf(float, int);
long double scalbnl(long double, int);

double scalbln(double, long);
float scalblnf(float, long);
long double scalblnl(long double, long);

/* ilogb/logb */
int ilogb(double);
int ilogbf(float);
int ilogbl(long double);

double logb(double);
float logbf(float);
long double logbl(long double);

/* nextafter/nexttoward */
double nextafter(double, double);
float nextafterf(float, float);
long double nextafterl(long double, long double);

double nexttoward(double, long double);
float nexttowardf(float, long double);
long double nexttowardl(long double, long double);

/* copysign */
double copysign(double, double);
float copysignf(float, float);
long double copysignl(long double, long double);

/* C23 IEC 60559:2020 interchange/classification functions (#774). These are
 * implemented as software bit-pattern functions in src/stdlib/math.c rather
 * than FFI wrappers: several (totalorder, totalordermag, fromfp/ufromfp,
 * getpayload/setpayload, llogb) are not exported by Darwin's libm at all,
 * and fmaximum/fminimum only landed in glibc 2.35, so FFI would need a
 * platform matrix for no benefit. */

/* fmaximum/fminimum family: unlike fmax/fmin, these propagate NaN (if
 * either argument is NaN, the result is NaN) and treat +0 as greater than
 * -0. The _num variants ignore a single NaN argument like fmax/fmin do,
 * but keep the +0 > -0 zero rule. The _mag variants compare by magnitude
 * (|x| vs |y|), falling back to the corresponding non-mag function on a
 * magnitude tie. */
double fmaximum(double, double);
float fmaximumf(float, float);
long double fmaximuml(long double, long double);

double fminimum(double, double);
float fminimumf(float, float);
long double fminimuml(long double, long double);

double fmaximum_num(double, double);
float fmaximum_numf(float, float);
long double fmaximum_numl(long double, long double);

double fminimum_num(double, double);
float fminimum_numf(float, float);
long double fminimum_numl(long double, long double);

double fmaximum_mag(double, double);
float fmaximum_magf(float, float);
long double fmaximum_magl(long double, long double);

double fminimum_mag(double, double);
float fminimum_magf(float, float);
long double fminimum_magl(long double, long double);

double fmaximum_mag_num(double, double);
float fmaximum_mag_numf(float, float);
long double fmaximum_mag_numl(long double, long double);

double fminimum_mag_num(double, double);
float fminimum_mag_numf(float, float);
long double fminimum_mag_numl(long double, long double);

/* totalorder/totalordermag: IEEE-754-2019 totalOrder predicate. Returns
 * nonzero iff x precedes y in the total order (-qNaN < -Inf < ... < -0 <
 * +0 < ... < +Inf < +qNaN, ordered by raw bit pattern within each NaN
 * sign). totalordermag compares |x| and |y| under the same order. */
int totalorder(double, double);
int totalorderf(float, float);
int totalorderl(long double, long double);

int totalordermag(double, double);
int totalordermagf(float, float);
int totalordermagl(long double, long double);

/* canonicalize: converts *x to canonical encoding and stores it in *cx,
 * returning 0 if *x was already canonical. CCCC's float/double are IEEE
 * 754 binary32/binary64, which (unlike decimal floating types) have no
 * non-canonical encodings, so this is a copy that always returns 0. */
int canonicalize(double *, const double *);
int canonicalizef(float *, const float *);
int canonicalizel(long double *, const long double *);

/* getpayload/setpayload/setpayload_sig: read/write a NaN's payload bits
 * (the significand bits below the quiet/signaling bit) as a nonnegative
 * double. getpayload returns -1 if *x is not a NaN. setpayload(_sig)
 * constructs a quiet (signaling) NaN with the given payload in *x and
 * returns 0 on success, nonzero if payload doesn't fit (negative,
 * fractional, too large, or -- for setpayload_sig -- zero, since a
 * signaling NaN's payload cannot be all-zero without becoming Inf). */
double getpayload(const double *);
float getpayloadf(const float *);
long double getpayloadl(const long double *);

int setpayload(double *, double);
int setpayloadf(float *, float);
int setpayloadl(long double *, long double);

int setpayloadsig(double *, double);
int setpayloadsigf(float *, float);
int setpayloadsigl(long double *, long double);

/* llogb: like ilogb but returns long. */
long llogb(double);
long llogbf(float);
long llogbl(long double);

/* fromfp/ufromfp family: round x to an integer value (returned in the
 * source floating type) per rounding direction `rnd`, that fits in a
 * `width`-bit signed (fromfp) or unsigned (ufromfp) integer; returns 0 if
 * it doesn't fit. The x variants also raise FE_INEXACT if the result
 * differs from x. */
#define FP_INT_UPWARD 0
#define FP_INT_DOWNWARD 1
#define FP_INT_TOWARDZERO 2
#define FP_INT_TONEARESTFROMZERO 3
#define FP_INT_TONEAREST 4

double fromfp(double, int, unsigned int);
float fromfpf(float, int, unsigned int);
long double fromfpl(long double, int, unsigned int);

double ufromfp(double, int, unsigned int);
float ufromfpf(float, int, unsigned int);
long double ufromfpl(long double, int, unsigned int);

double fromfpx(double, int, unsigned int);
float fromfpxf(float, int, unsigned int);
long double fromfpxl(long double, int, unsigned int);

double ufromfpx(double, int, unsigned int);
float ufromfpxf(float, int, unsigned int);
long double ufromfpxl(long double, int, unsigned int);

/* issignaling/iseqsig/iscanonical: macro-level classification (C23).
 * issignaling(x) tests the quiet bit directly on the raw bit pattern (a
 * signaling NaN would quiet itself if compared or arithmetic'd on, so this
 * cannot be implemented via isnan()). iseqsig(x, y) is an equality compare
 * that raises FE_INVALID for a signaling NaN operand (unlike ==, which
 * only does so for quiet-vs-quiet). iscanonical is always true for
 * IEEE-754 binary formats (see canonicalize above). */
int __cccc_issignaling_f(float);
int __cccc_issignaling_d(double);
#define issignaling(x) _Generic((x), float: __cccc_issignaling_f, default: __cccc_issignaling_d)(x)

int __cccc_iseqsig_f(float, float);
int __cccc_iseqsig_d(double, double);
#define iseqsig(x, y) _Generic((x), float: __cccc_iseqsig_f, default: __cccc_iseqsig_d)((x), (y))

#define iscanonical(x) ((void)(x), 1)

#endif /* __MATH_H */
