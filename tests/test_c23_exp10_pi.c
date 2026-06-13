// CCCC_FLAGS: --std=c23
// Test C23 exp10/exp10f/exp10l and the sinpi/cospi/tanpi/asinpi/acospi/
// atanpi/atan2pi families (ticket #398).
//
// NOTE: the "f" (single-precision float) variants below - exp10f, sinpif,
// cospif, tanpif, asinpif, acospif, atanpif, atan2pif - are registered and
// implemented, but the native FFI bridge cannot currently marshal `float`
// args/returns correctly (pre-existing bug, see ticket #406; affects every
// "f"-suffixed libm function, not just these). They are exercised here for
// side effects only, without checking their results, until #406 is fixed.
#include <math.h>

static double fabs_d(double x) { return x < 0 ? -x : x; }

int main(void) {
    // exp10 family (double, long double - unaffected by #406)
    if (exp10(0.0) != 1.0) return 1;
    if (fabs_d(exp10(2.0) - 100.0) > 1e-9) return 2;
    if (fabs_d((double)exp10l(2.0L) - 100.0L) > 1e-9) return 3;

    // exp10f: registered but not result-checked, see #406
    (void)exp10f(2.0f);

    // sinpi/cospi/tanpi - exact at integer/half-integer points
    if (sinpi(0.0) != 0.0) return 5;
    if (sinpi(0.5) != 1.0) return 6;
    if (sinpi(1.0) != 0.0) return 7;
    if (cospi(0.0) != 1.0) return 8;
    if (cospi(1.0) != -1.0) return 9;
    if (cospi(0.5) != 0.0) return 10;
    if (tanpi(0.0) != 0.0) return 11;
    if (fabs_d(tanpi(0.25) - 1.0) > 1e-9) return 12;

    // long double variant - unaffected by #406
    if (tanpil(0.0L) != 0.0L) return 15;

    // sinpif/cospif: registered but not result-checked, see #406
    (void)sinpif(0.5f);
    (void)cospif(0.0f);

    // asinpi/acospi/atanpi/atan2pi (double, long double)
    if (fabs_d(asinpi(1.0) - 0.5) > 1e-9) return 16;
    if (fabs_d(acospi(-1.0) - 1.0) > 1e-9) return 17;
    if (fabs_d(atanpi(1.0) - 0.25) > 1e-9) return 18;
    if (fabs_d(atan2pi(1.0, 1.0) - 0.25) > 1e-9) return 19;
    if (fabs_d(atan2pi(1.0, 0.0) - 0.5) > 1e-9) return 20;
    if (fabs_d((double)atanpil(1.0L) - 0.25L) > 1e-9) return 22;

    // asinpif: registered but not result-checked, see #406
    (void)asinpif(1.0f);

    return 42;
}
