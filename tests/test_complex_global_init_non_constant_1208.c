// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not a compile-time constant
//
// #1208: eval_complex folds a _Complex constant expression for a file-scope
// initializer, mirroring eval_double's node-kind table. An initializer that
// reads a mutable global has no constant value -- eval_complex must reject
// it with the same "not a compile-time constant" diagnostic eval_double
// gives, not crash or silently fold to zero.

#include <complex.h>

static double _Complex src = 1.0 + 2.0 * I;
static double _Complex bad = src + 3.0 * I;

int main(void) {
    return 42;
}
