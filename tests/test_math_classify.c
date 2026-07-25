// Regression test for #778: isnan/isinf were referenced by isfinite/
// fpclassify but never defined anywhere (not as macros, not as registered
// functions, not reachable via the bare name -- only __builtin_isnan/
// __builtin_isinf existed), so isfinite/isnormal/fpclassify failed to
// compile. signbit(x) ((x) < 0) compiled but was wrong for -0.0 and NaN.
// fpclassify could never return FP_ZERO, and isnormal was true for
// subnormals. Fixed with real bit-pattern functions dispatched via
// _Generic, following the issignaling/iseqsig pattern added for #774.
#include <math.h>

int main(void) {
    double nan_val = 0.0 / 0.0; // default quiet NaN, zero payload

    if (!isnan(nan_val)) return 1;
    if (isnan(1.0)) return 2;
    if (isnan(0.0)) return 3;

    if (!isinf(INFINITY)) return 4;
    if (isinf(1.0)) return 5;
    if (isinf(nan_val)) return 6; // the original bug: this used to misclassify as Inf

    // signbit: correct for -0.0 and NaN, unlike the old `x < 0` formula.
    if (signbit(0.0)) return 7;
    if (!signbit(-0.0)) return 8;
    if (signbit(5.0)) return 9;
    if (!signbit(-5.0)) return 10;

    // fpclassify: FP_ZERO for 0.0 (the old formula could never produce
    // this), FP_NAN for NaN (not FP_INFINITE, the original bug),
    // FP_SUBNORMAL for a subnormal, FP_NORMAL/FP_INFINITE otherwise.
    if (fpclassify(0.0) != FP_ZERO) return 11;
    if (fpclassify(1.0) != FP_NORMAL) return 12;
    if (fpclassify(nan_val) != FP_NAN) return 13;
    if (fpclassify(INFINITY) != FP_INFINITE) return 14;
    double subnorm = DBL_TRUE_MIN;
    if (fpclassify(subnorm) != FP_SUBNORMAL) return 15;

    // isnormal: false for subnormals (the old formula only excluded
    // NaN/Inf/zero, not the subnormal range).
    if (isnormal(subnorm)) return 16;
    if (!isnormal(1.0)) return 17;
    if (isnormal(0.0)) return 18;

    if (!isfinite(1.0)) return 19;
    if (isfinite(INFINITY)) return 20;
    if (isfinite(nan_val)) return 21;

    // float variants (dispatched via _Generic on the float type).
    float fnan = (float)nan_val;
    if (!isnan(fnan)) return 22;
    if (!signbit(-5.0f)) return 23;
    if (fpclassify(0.0f) != FP_ZERO) return 24;

    return 42;
}
