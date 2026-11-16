// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: complex arguments through variadic FFI calls are not supported
// CCCC_NATIVE_SKIP: the VM-only diagnostic does not apply to -c=native

#include <complex.h>
#include <stdio.h>

int main(void) {
    double _Complex z = CMPLX(1.0, 2.0);
    printf("%f\n", z);
    return 0;
}
