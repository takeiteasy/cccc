#include <complex.h>
#include <tgmath.h>

int main(void) {
    double complex z = CMPLX(3.0, 4.0);
    if (fabs(z) != 5.0) return 1;
    if (cabs(z) != 5.0) return 2;
    if (carg(CMPLX(1.0, 0.0)) != 0.0) return 3;

    double complex i = I;
    if (creal(i) != 0.0) return 4;
    if (cimag(i) != 1.0) return 5;

    double _Imaginary compat = I;
    if (creal(compat) != 0.0) return 6;
    if (cimag(compat) != 1.0) return 7;

    return 42;
}
