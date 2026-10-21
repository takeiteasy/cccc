// right-nested complex binops and a funcall in the RHS match -c=native
#include <complex.h>
static double helper(void) {
    return 7.0;
}
int main(void) {
    double complex z = 20.0 + 22.0 * I;
    if (creal(z) != 20.0 || cimag(z) != 22.0)
        return 1;
    double complex a     = __cccc_cmplx(1.0, 2.0);
    double complex b     = __cccc_cmplx(3.0, 4.0);
    double complex c     = __cccc_cmplx(5.0, 6.0);
    double complex mul_r = a * (b * c);
    if (creal(mul_r) != -85.0 || cimag(mul_r) != 20.0)
        return 2;
    double complex m = a * (2.0 + helper());
    if (creal(m) != 9.0 || cimag(m) != 18.0)
        return 3;
    return 42;
}
