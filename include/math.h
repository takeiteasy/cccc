/* math.h - math functions for JCC C compiler */

#ifndef __MATH_H
#define __MATH_H

#include "stddef.h"
#include "float.h"
#include "limits.h"
#include "stdint.h"

/* Floating-point constants */
#define HUGE_VAL (__jcc_huge_val())
#ifndef HUGE_VALF
#define HUGE_VALF ((float)HUGE_VAL)
#endif
#ifndef HUGE_VALL
#define HUGE_VALL ((long double)HUGE_VAL)
#endif
#define INFINITY (__jcc_inff())
#define NAN (__jcc_nanf(""))

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

#define isfinite(x) (!(isnan(x) || isinf(x)))
#define signbit(x) ((x) < 0)
#define isnormal(x) (isfinite(x) && ((x) != 0.0))
#define fpclassify(x) (isnan(x) ? FP_NAN : (isinf(x) ? FP_INFINITE : (isfinite(x) ? FP_NORMAL : FP_SUBNORMAL)))

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

#endif /* __MATH_H */
