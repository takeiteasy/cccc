// math.h stdlib function registration
#include "../cccc.h"
#include <math.h>

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

// Register all math.h functions
void register_math_functions(VirtualMachine *vm) {
    // Basic operations
    cc_register_cfunc(vm, "fabs", (void*)fabs, 1, 1);
    cc_register_cfunc(vm, "fabsf", (void*)fabsf, 1, 2);
    cc_register_cfunc(vm, "fabsl", (void*)fabs, 1, 1);
    cc_register_cfunc_ex(vm, "fmod", (void*)fmod, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmodf", (void*)fmodf, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "fmodl", (void*)fmod, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remainder", (void*)remainder, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remainderf", (void*)remainderf, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "remainderl", (void*)remainder, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remquo", (void*)remquo, 3, 0, 0b11);  // double, double, int*
    cc_register_cfunc_ex(vm, "remquof", (void*)remquof, 3, 2, 0b11);    cc_register_cfunc_ex(vm, "remquol", (void*)remquo, 3, 0, 0b11);
    cc_register_cfunc_ex(vm, "fma", (void*)fma, 3, 1, 0b111);  // double, double, double
    cc_register_cfunc_ex(vm, "fmaf", (void*)fmaf, 3, 2, 0b111);    cc_register_cfunc_ex(vm, "fmal", (void*)fma, 3, 1, 0b111);
    cc_register_cfunc_ex(vm, "fmax", (void*)fmax, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmaxf", (void*)fmaxf, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "fmaxl", (void*)fmax, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmin", (void*)fmin, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fminf", (void*)fminf, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "fminl", (void*)fmin, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fdim", (void*)fdim, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fdimf", (void*)fdimf, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "fdiml", (void*)fdim, 2, 1, 0b11);
    cc_register_cfunc(vm, "nan", (void*)nan, 1, 1);
    cc_register_cfunc(vm, "nanf", (void*)nanf, 1, 2);    cc_register_cfunc(vm, "nanl", (void*)nan, 1, 1);

    // Exponential/logarithmic - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "exp", (void*)exp, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expf", (void*)expf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "expl", (void*)exp, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "exp2", (void*)exp2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "exp2f", (void*)exp2f, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "exp2l", (void*)exp2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expm1", (void*)expm1, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expm1f", (void*)expm1f, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "expm1l", (void*)expm1, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log", (void*)log, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "logf", (void*)logf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "logl", (void*)log, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log10", (void*)log10, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log10f", (void*)log10f, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "log10l", (void*)log10, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log2", (void*)log2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log2f", (void*)log2f, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "log2l", (void*)log2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log1p", (void*)log1p, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log1pf", (void*)log1pf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "log1pl", (void*)log1p, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "pow", (void*)pow, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "powf", (void*)powf, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "powl", (void*)pow, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "sqrt", (void*)sqrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sqrtf", (void*)sqrtf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "sqrtl", (void*)sqrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cbrt", (void*)cbrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cbrtf", (void*)cbrtf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "cbrtl", (void*)cbrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "hypot", (void*)hypot, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "hypotf", (void*)hypotf, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "hypotl", (void*)hypot, 2, 1, 0b11);

    // Trigonometric - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "sin", (void*)sin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sinf", (void*)sinf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "sinl", (void*)sin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cos", (void*)cos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cosf", (void*)cosf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "cosl", (void*)cos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tan", (void*)tan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanf", (void*)tanf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "tanl", (void*)tan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asin", (void*)asin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinf", (void*)asinf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "asinl", (void*)asin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acos", (void*)acos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acosf", (void*)acosf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "acosl", (void*)acos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atan", (void*)atan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanf", (void*)atanf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "atanl", (void*)atan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atan2", (void*)atan2, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "atan2f", (void*)atan2f, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "atan2l", (void*)atan2, 2, 1, 0b11);

    // Hyperbolic - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "sinh", (void*)sinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sinhf", (void*)sinhf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "sinhl", (void*)sinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cosh", (void*)cosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "coshf", (void*)coshf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "coshl", (void*)cosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanh", (void*)tanh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanhf", (void*)tanhf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "tanhl", (void*)tanh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinh", (void*)asinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinhf", (void*)asinhf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "asinhl", (void*)asinh, 1, 1, 0b1);

    // Special functions - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "erf", (void*)erf, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erff", (void*)erff, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "erfl", (void*)erf, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erfc", (void*)erfc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erfcf", (void*)erfcf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "erfcl", (void*)erfc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tgamma", (void*)tgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tgammaf", (void*)tgammaf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "tgammal", (void*)tgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lgamma", (void*)lgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lgammaf", (void*)lgammaf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "lgammal", (void*)lgamma, 1, 1, 0b1);

    // Rounding - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "ceil", (void*)ceil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "ceilf", (void*)ceilf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "ceill", (void*)ceil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "floor", (void*)floor, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "floorf", (void*)floorf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "floorl", (void*)floor, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "trunc", (void*)trunc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "truncf", (void*)truncf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "truncl", (void*)trunc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "round", (void*)round, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "roundf", (void*)roundf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "roundl", (void*)round, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lround", (void*)lround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lroundf", (void*)lroundf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lroundl", (void*)lround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llround", (void*)llround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llroundf", (void*)llroundf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llroundl", (void*)llround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "nearbyint", (void*)nearbyint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "nearbyintf", (void*)nearbyintf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "nearbyintl", (void*)nearbyint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "rint", (void*)rint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "rintf", (void*)rintf, 1, 2, 0b1);    cc_register_cfunc_ex(vm, "rintl", (void*)rint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lrint", (void*)lrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lrintf", (void*)lrintf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lrintl", (void*)lrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llrint", (void*)llrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llrintf", (void*)llrintf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llrintl", (void*)llrint, 1, 0, 0b1);

    // Manipulation
    cc_register_cfunc(vm, "frexp", (void*)frexp, 3, 0);
    cc_register_cfunc(vm, "frexpf", (void*)frexpf, 3, 2);
    cc_register_cfunc(vm, "frexpl", (void*)frexp, 3, 0);
    cc_register_cfunc_ex(vm, "ldexp", (void*)ldexp, 2, 1, 0b01);  // double, int
    cc_register_cfunc_ex(vm, "ldexpf", (void*)ldexpf, 2, 2, 0b01);    cc_register_cfunc_ex(vm, "ldexpl", (void*)ldexp, 2, 1, 0b01);
    cc_register_cfunc_ex(vm, "modf", (void*)modf, 2, 0, 0b01);  // double, double*
    cc_register_cfunc_ex(vm, "modff", (void*)modff, 2, 2, 0b01);    cc_register_cfunc_ex(vm, "modfl", (void*)modf, 2, 0, 0b01);
    cc_register_cfunc_ex(vm, "scalbn", (void*)scalbn, 2, 1, 0b01);  // double, int
    cc_register_cfunc_ex(vm, "scalbnf", (void*)scalbnf, 2, 2, 0b01);    cc_register_cfunc_ex(vm, "scalbnl", (void*)scalbn, 2, 1, 0b01);
    cc_register_cfunc_ex(vm, "scalbln", (void*)scalbln, 2, 1, 0b01);  // double, long
    cc_register_cfunc_ex(vm, "scalblnf", (void*)scalblnf, 2, 2, 0b01);    cc_register_cfunc_ex(vm, "scalblnl", (void*)scalbln, 2, 1, 0b01);
    cc_register_cfunc(vm, "ilogb", (void*)ilogb, 1, 1);
    cc_register_cfunc(vm, "ilogbf", (void*)ilogbf, 1, 0);
    cc_register_cfunc(vm, "ilogbl", (void*)ilogb, 1, 1);
    cc_register_cfunc(vm, "logb", (void*)logb, 1, 1);
    cc_register_cfunc(vm, "logbf", (void*)logbf, 1, 2);    cc_register_cfunc(vm, "logbl", (void*)logb, 1, 1);
    cc_register_cfunc_ex(vm, "nextafter", (void*)nextafter, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "nextafterf", (void*)nextafterf, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "nextafterl", (void*)nextafter, 2, 1, 0b11);
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
    cc_register_cfunc_ex(vm, "copysignf", (void*)copysignf, 2, 2, 0b11);    cc_register_cfunc_ex(vm, "copysignl", (void*)copysign, 2, 1, 0b11);

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
}
