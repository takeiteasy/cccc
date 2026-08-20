// Regression test for #772: <float.h> FLT_* macros were aliased to the
// DBL_* (double-precision) values even though CCCC's `float` is a genuine
// 32-bit type. Verifies the corrected single-precision limits and the
// missing C11/C23 macros that were added alongside the fix.
#include <float.h>

static int approx(double a, double b, double tol) {
    double d = a - b;
    if (d < 0)
        d = -d;
    return d < tol;
}

int main(void) {
    // Single precision must differ from double precision.
    if (FLT_MANT_DIG != 24)
        return 1;
    if (DBL_MANT_DIG != 53)
        return 2;
    if (FLT_MANT_DIG == DBL_MANT_DIG)
        return 3;

    if (FLT_MAX_EXP != 128)
        return 4;
    if (FLT_MIN_EXP != -125)
        return 5;
    if (FLT_MAX_10_EXP != 38)
        return 6;
    if (FLT_MIN_10_EXP != -37)
        return 7;

    if (!approx((double)FLT_MAX, 3.40282347e+38, 1e+31))
        return 8;
    if (!approx((double)FLT_MIN, 1.17549435e-38, 1e-45))
        return 9;
    if (!approx((double)FLT_EPSILON, 1.19209290e-07, 1e-14))
        return 10;

    // FLT_MAX must NOT equal DBL_MAX (the historical bug).
    if ((double)FLT_MAX == DBL_MAX)
        return 11;
    if ((double)FLT_EPSILON == DBL_EPSILON)
        return 12;

    // LDBL_* == DBL_* is legal and correct here (long double is 64-bit).
    if (LDBL_MANT_DIG != DBL_MANT_DIG)
        return 13;
    if (LDBL_MAX != DBL_MAX)
        return 14;

    // C11 additions.
    if (DECIMAL_DIG < FLT_DECIMAL_DIG)
        return 15;
    if (FLT_HAS_SUBNORM != 1 || DBL_HAS_SUBNORM != 1)
        return 16;
    if ((double)FLT_TRUE_MIN <= 0.0 || (double)FLT_TRUE_MIN >= (double)FLT_MIN)
        return 17;
    if (DBL_TRUE_MIN <= 0.0 || DBL_TRUE_MIN >= DBL_MIN)
        return 18;

    // C23 additions.
    if ((double)FLT_NORM_MAX != (double)FLT_MAX)
        return 19;
    if (DBL_NORM_MAX != DBL_MAX)
        return 20;

    float  fs = FLT_SNAN;
    double ds = DBL_SNAN;
    if (!(fs != fs))
        return 21; // must be NaN
    if (!(ds != ds))
        return 22; // must be NaN

    return 42;
}
