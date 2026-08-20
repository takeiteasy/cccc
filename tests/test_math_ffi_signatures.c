// Regression test for #777: several cc_register_cfunc/cc_register_cfunc_ex
// calls in src/stdlib/math.c (and src/stdlib/stdlib.c, src/stdlib/wide.c)
// had hand-typo'd num_args/returns_double/double_arg_mask for functions
// with pointer out-params or float/int returns, silently producing
// garbage. None of frexp/modf/remquo/ilogb had a test before, which is
// how this went unnoticed.
//
// Root causes fixed:
//   - frexp/frexpl: registered with num_args=3 (should be 2 -- the real
//     signature is (double, int*), not three args) and returns_double=0
//     (should be 1 -- frexp returns double).
//   - modf/modfl: returns_double=0 (should be 1 -- modf returns double).
//   - remquo/remquol: returns_double=0 (should be 1 -- remquo returns
//     double, unlike the mistakenly int-returning declaration this also
//     fixed in include/math.h). remquof was registered returns_double=2
//     to match that wrong int-returning declaration; now 2 is correct
//     again since remquof genuinely returns float.
//   - ilogb/ilogbl: returns_double=1 (should be 0 -- ilogb returns int).
//   - abs/labs/llabs (stdlib.c), strtof/wcstof (stdlib.c/wide.c):
//     returns_double set to 1 for integer/float returning functions.
//   - acosh/atanh (all three width variants): declared in include/math.h
//     but never registered at all.
//
// Also fixed as part of the same audit pass (tools/audit_ffi.py): ~70
// double_arg_mask entries for float-typed arguments that were carrying
// the double bit (e.g. fmodf registered with mask 0b11 instead of 0).
// Direct calls recompute this mask from the call-site's static types in
// codegen, so those specific mismatches aren't independently observable
// through a normal call -- audit_ffi.py is the regression guard for them.
#include <math.h>
#include <stdlib.h>

int main(void) {
    // frexp: 8.0 = 0.5 * 2^4
    int    e = 0;
    double m = frexp(8.0, &e);
    if (m != 0.5 || e != 4)
        return 1;

    float mf = frexpf(8.0f, &e);
    if (mf != 0.5f || e != 4)
        return 2;

    // modf: integral and fractional parts, sign-preserving.
    double ip = 0;
    double fp = modf(3.75, &ip);
    if (ip != 3.0 || fp != 0.75)
        return 3;

    float ipf = 0;
    float fpf = modff(3.75f, &ipf);
    if (ipf != 3.0f || fpf != 0.75f)
        return 4;

    // remquo: 29 = 4*7 + 1, quotient bits rounded to nearest (4).
    int    q = 0;
    double r = remquo(29.0, 7.0, &q);
    if (r != 1.0 || q != 4)
        return 5;

    q        = 0;
    float rf = remquof(29.0f, 7.0f, &q);
    if (rf != 1.0f || q != 4)
        return 6;

    // ilogb: exponent of the binary representation.
    if (ilogb(8.0) != 3)
        return 7;
    if (ilogbf(8.0f) != 3)
        return 8;
    if (ilogbl(8.0L) != 3)
        return 9;

    // logb: like ilogb but returns a double.
    if (logb(8.0) != 3.0)
        return 10;

    // acosh/atanh: newly registered (were entirely missing).
    if (acosh(1.0) != 0.0)
        return 11;
    double half_ln3 = 0.5493061443340549; // atanh(0.5)
    double d        = atanh(0.5) - half_ln3;
    if (d < 0)
        d = -d;
    if (d > 1e-9)
        return 12;

    // abs/labs/llabs: integer-returning, not double-returning.
    if (abs(-5) != 5)
        return 13;
    if (labs(-7L) != 7L)
        return 14;
    if (llabs(-9LL) != 9LL)
        return 15;

    // strtof: float-returning.
    float sf = strtof("3.5", NULL);
    if (sf != 3.5f)
        return 16;

    return 42;
}
