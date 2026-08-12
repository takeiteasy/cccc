// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __builtin_creal\(
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// `_Complex` construction and the creal/cimag/conj projections share one
// node kind, discriminated by `val`. Construction maps to
// `__builtin_complex`, which requires both operands to have the same real
// floating type — hence the explicit casts to the element type in the
// emitted form — and the projections map to `__builtin_creal`/`cimag`/
// `conj` with the f/l suffix chosen from the element type.
//
// The companion `tests/test_serialize_type_complex.c` covers the *type*
// half (`_Complex double`); this covers the expressions.

#include <complex.h>

int main(void) {
    double complex z = __cccc_cmplx(20.0, 22.0);
    double complex c = conj(z);
    return (int)creal(z) + (int)cimag(z) - (int)cimag(c) - 22;
}
