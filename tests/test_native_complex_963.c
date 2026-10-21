// -c=native, _Complex construction and creal/cimag/conj round-trip
#include <complex.h>
int main(void) {
    double complex z = __cccc_cmplx(20.0, 22.0);
    double complex c = conj(z);
    return (int)creal(z) + (int)cimag(z) - (int)cimag(c) - 22;
}
