// math.h stdlib function registration
#include "../cccc.h"
#include <math.h>

// Register all math.h functions
void register_math_functions(CCCC *vm) {
    // Basic operations
    cc_register_cfunc(vm, "fabs", (void*)fabs, 1, 1);
    cc_register_cfunc(vm, "fabsf", (void*)fabsf, 1, 1);
    cc_register_cfunc(vm, "fabsl", (void*)fabsl, 1, 1);
    cc_register_cfunc_ex(vm, "fmod", (void*)fmod, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmodf", (void*)fmodf, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "fmodl", (void*)fmodl, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remainder", (void*)remainder, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remainderf", (void*)remainderf, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "remainderl", (void*)remainderl, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "remquo", (void*)remquo, 3, 0, 0b11);  // double, double, int*
    cc_register_cfunc_ex(vm, "remquof", (void*)remquof, 3, 0, 0b11);
    cc_register_cfunc_ex(vm, "remquol", (void*)remquol, 3, 0, 0b11);
    cc_register_cfunc_ex(vm, "fma", (void*)fma, 3, 1, 0b111);  // double, double, double
    cc_register_cfunc_ex(vm, "fmaf", (void*)fmaf, 3, 0, 0b111);
    cc_register_cfunc_ex(vm, "fmal", (void*)fmal, 3, 1, 0b111);
    cc_register_cfunc_ex(vm, "fmax", (void*)fmax, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmaxf", (void*)fmaxf, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "fmaxl", (void*)fmaxl, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fmin", (void*)fmin, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fminf", (void*)fminf, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "fminl", (void*)fminl, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fdim", (void*)fdim, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "fdimf", (void*)fdimf, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "fdiml", (void*)fdiml, 2, 1, 0b11);
    cc_register_cfunc(vm, "nan", (void*)nan, 1, 1);
    cc_register_cfunc(vm, "nanf", (void*)nanf, 1, 0);
    cc_register_cfunc(vm, "nanl", (void*)nanl, 1, 1);

    // Exponential/logarithmic - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "exp", (void*)exp, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expf", (void*)expf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "expl", (void*)expl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "exp2", (void*)exp2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "exp2f", (void*)exp2f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "exp2l", (void*)exp2l, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expm1", (void*)expm1, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "expm1f", (void*)expm1f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "expm1l", (void*)expm1l, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log", (void*)log, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "logf", (void*)logf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "logl", (void*)logl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log10", (void*)log10, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log10f", (void*)log10f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "log10l", (void*)log10l, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log2", (void*)log2, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log2f", (void*)log2f, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "log2l", (void*)log2l, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log1p", (void*)log1p, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "log1pf", (void*)log1pf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "log1pl", (void*)log1pl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "pow", (void*)pow, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "powf", (void*)powf, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "powl", (void*)powl, 2, 1, 0b11);
    cc_register_cfunc_ex(vm, "sqrt", (void*)sqrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sqrtf", (void*)sqrtf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "sqrtl", (void*)sqrtl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cbrt", (void*)cbrt, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cbrtf", (void*)cbrtf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "cbrtl", (void*)cbrtl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "hypot", (void*)hypot, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "hypotf", (void*)hypotf, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "hypotl", (void*)hypotl, 2, 1, 0b11);

    // Trigonometric - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "sin", (void*)sin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sinf", (void*)sinf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "sinl", (void*)sinl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cos", (void*)cos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cosf", (void*)cosf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "cosl", (void*)cosl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tan", (void*)tan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanf", (void*)tanf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "tanl", (void*)tanl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asin", (void*)asin, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinf", (void*)asinf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "asinl", (void*)asinl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acos", (void*)acos, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "acosf", (void*)acosf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "acosl", (void*)acosl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atan", (void*)atan, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atanf", (void*)atanf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "atanl", (void*)atanl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "atan2", (void*)atan2, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "atan2f", (void*)atan2f, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "atan2l", (void*)atan2l, 2, 1, 0b11);

    // Hyperbolic - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "sinh", (void*)sinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "sinhf", (void*)sinhf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "sinhl", (void*)sinhl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "cosh", (void*)cosh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "coshf", (void*)coshf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "coshl", (void*)coshl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanh", (void*)tanh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tanhf", (void*)tanhf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "tanhl", (void*)tanhl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinh", (void*)asinh, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "asinhf", (void*)asinhf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "asinhl", (void*)asinhl, 1, 1, 0b1);

    // Special functions - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "erf", (void*)erf, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erff", (void*)erff, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "erfl", (void*)erfl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erfc", (void*)erfc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "erfcf", (void*)erfcf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "erfcl", (void*)erfcl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tgamma", (void*)tgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "tgammaf", (void*)tgammaf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "tgammal", (void*)tgammal, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lgamma", (void*)lgamma, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lgammaf", (void*)lgammaf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lgammal", (void*)lgammal, 1, 1, 0b1);

    // Rounding - single double arg needs mask 0b1
    cc_register_cfunc_ex(vm, "ceil", (void*)ceil, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "ceilf", (void*)ceilf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "ceill", (void*)ceill, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "floor", (void*)floor, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "floorf", (void*)floorf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "floorl", (void*)floorl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "trunc", (void*)trunc, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "truncf", (void*)truncf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "truncl", (void*)truncl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "round", (void*)round, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "roundf", (void*)roundf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "roundl", (void*)roundl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lround", (void*)lround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lroundf", (void*)lroundf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lroundl", (void*)lroundl, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llround", (void*)llround, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llroundf", (void*)llroundf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llroundl", (void*)llroundl, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "nearbyint", (void*)nearbyint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "nearbyintf", (void*)nearbyintf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "nearbyintl", (void*)nearbyintl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "rint", (void*)rint, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "rintf", (void*)rintf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "rintl", (void*)rintl, 1, 1, 0b1);
    cc_register_cfunc_ex(vm, "lrint", (void*)lrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lrintf", (void*)lrintf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "lrintl", (void*)lrintl, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llrint", (void*)llrint, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llrintf", (void*)llrintf, 1, 0, 0b1);
    cc_register_cfunc_ex(vm, "llrintl", (void*)llrintl, 1, 0, 0b1);

    // Manipulation
    cc_register_cfunc(vm, "frexp", (void*)frexp, 3, 0);
    cc_register_cfunc(vm, "frexpf", (void*)frexpf, 3, 0);
    cc_register_cfunc(vm, "frexpl", (void*)frexpl, 3, 0);
    cc_register_cfunc_ex(vm, "ldexp", (void*)ldexp, 2, 1, 0b01);  // double, int
    cc_register_cfunc_ex(vm, "ldexpf", (void*)ldexpf, 2, 0, 0b01);
    cc_register_cfunc_ex(vm, "ldexpl", (void*)ldexpl, 2, 1, 0b01);
    cc_register_cfunc_ex(vm, "modf", (void*)modf, 2, 0, 0b01);  // double, double*
    cc_register_cfunc_ex(vm, "modff", (void*)modff, 2, 0, 0b01);
    cc_register_cfunc_ex(vm, "modfl", (void*)modfl, 2, 0, 0b01);
    cc_register_cfunc_ex(vm, "scalbn", (void*)scalbn, 2, 1, 0b01);  // double, int
    cc_register_cfunc_ex(vm, "scalbnf", (void*)scalbnf, 2, 0, 0b01);
    cc_register_cfunc_ex(vm, "scalbnl", (void*)scalbnl, 2, 1, 0b01);
    cc_register_cfunc_ex(vm, "scalbln", (void*)scalbln, 2, 1, 0b01);  // double, long
    cc_register_cfunc_ex(vm, "scalblnf", (void*)scalblnf, 2, 0, 0b01);
    cc_register_cfunc_ex(vm, "scalblnl", (void*)scalblnl, 2, 1, 0b01);
    cc_register_cfunc(vm, "ilogb", (void*)ilogb, 1, 1);
    cc_register_cfunc(vm, "ilogbf", (void*)ilogbf, 1, 0);
    cc_register_cfunc(vm, "ilogbl", (void*)ilogbl, 1, 1);
    cc_register_cfunc(vm, "logb", (void*)logb, 1, 1);
    cc_register_cfunc(vm, "logbf", (void*)logbf, 1, 0);
    cc_register_cfunc(vm, "logbl", (void*)logbl, 1, 1);
    cc_register_cfunc_ex(vm, "nextafter", (void*)nextafter, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "nextafterf", (void*)nextafterf, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "nextafterl", (void*)nextafterl, 2, 1, 0b11);
    cc_register_cfunc(vm, "nexttoward", (void*)nexttoward, 2, 1);
    cc_register_cfunc(vm, "nexttowardf", (void*)nexttowardf, 2, 0);
    cc_register_cfunc(vm, "nexttowardl", (void*)nexttowardl, 2, 1);
    cc_register_cfunc_ex(vm, "copysign", (void*)copysign, 2, 1, 0b11);  // double, double
    cc_register_cfunc_ex(vm, "copysignf", (void*)copysignf, 2, 0, 0b11);
    cc_register_cfunc_ex(vm, "copysignl", (void*)copysignl, 2, 1, 0b11);
}
