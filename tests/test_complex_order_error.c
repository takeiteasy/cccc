// EXPECT_COMPILE_ERROR
#include <complex.h>

int main(void) {
    double complex z = CMPLX(1.0, 2.0);
    return z < CMPLX(2.0, 3.0);
}
