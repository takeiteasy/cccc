// Regression test for #773: FDIV3/FDIV3_F32 used to abort the VM on any
// float division by zero. Unlike integer division by zero (genuinely UB),
// IEEE-754 finite/0.0 is well-defined (a correctly-signed infinity,
// raising FE_DIVBYZERO) and 0.0/0.0 is NaN (raising FE_INVALID) -- neither
// is UB, so plain IEEE division is now the default (no --trap-fp-divzero).
#include <fenv.h>

int main(void) {
    volatile double pzero    = 0.0;
    volatile double nzero    = -0.0;
    volatile float  fzero    = 0.0f;

    double          pos_inf  = 1.0 / pzero;
    double          neg_inf  = -1.0 / pzero;
    double          nan_val  = 0.0 / pzero;
    float           fpos_inf = 1.0f / fzero;

    // +Inf: larger than any finite double, and equal to itself.
    if (!(pos_inf > 1e300 && pos_inf == pos_inf))
        return 1;
    // -Inf: smaller than any finite double, and equal to itself.
    if (!(neg_inf < -1e300 && neg_inf == neg_inf))
        return 2;
    // NaN: not equal to itself.
    if (!(nan_val != nan_val))
        return 3;
    // float +Inf.
    if (!(fpos_inf > 1e30f && fpos_inf == fpos_inf))
        return 4;

    // Dividing by -0.0 flips the sign versus dividing by +0.0.
    double neg_zero_div = 1.0 / nzero;
    if (!(neg_zero_div < -1e300))
        return 5;

    // The host division actually raises FE_DIVBYZERO -- observable now
    // that #771 wired up the real host FE_* constants.
    feclearexcept(FE_ALL_EXCEPT);
    double r = 1.0 / pzero;
    (void)r;
    if (!(fetestexcept(FE_DIVBYZERO)))
        return 6;

    feclearexcept(FE_ALL_EXCEPT);
    double s = 0.0 / pzero;
    (void)s;
    if (!(fetestexcept(FE_INVALID)))
        return 7;

    return 42;
}
