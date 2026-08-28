// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __builtin_complex\(
// CCCC_REJECT_STDOUT: cannot serialize initializer
//
// #1208: a file-scope `_Complex` object whose initializer has a non-zero
// imaginary part (here via `I` from complex.h, which desugars to an
// ND_COMPLEX node) folds at compile time (eval_complex) and serializes
// under -m / -c=native as `__builtin_complex((elem)re, (elem)im)` -- the
// same shape serialize_expr's ND_COMPLEX construction arm emits, accepted
// by clang and gcc in a static initializer. Before #1208 the front end
// rejected the initializer as "not a compile-time constant" before the
// serializer was ever reached.
//
// A zero-imaginary complex global (`zr` below) still prints as a bare real
// literal -- byte-identical to pre-#1208 output -- so only the non-zero
// cases are asserted here.

#include <complex.h>

static double _Complex zc      = 3.0 + 4.0 * I;
static float _Complex zf       = 1.5f + 2.5f * I;
static long double _Complex zl = 5.0L + 6.0L * I;
static double _Complex zr      = 7.0;

int main(void) {
    return (int)creal(zc) + (int)cimag(zc) + (int)crealf(zf) + (int)cimagf(zf) +
                       (int)creall(zl) + (int)cimagl(zl) + (int)creal(zr) ==
                   3 + 4 + 1 + 2 + 5 + 6 + 7
               ? 42
               : 0;
}
