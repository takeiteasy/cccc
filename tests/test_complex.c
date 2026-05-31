#include <complex.h>

struct Box {
    double complex z;
};

int main(void) {
    double _Complex z = CMPLX(1.0, 2.0);
    if (sizeof(z) != 16) return 1;
    if (_Alignof(double _Complex) != 8) return 2;
    if (creal(z) != 1.0) return 3;
    if (cimag(z) != 2.0) return 4;

    double complex w = z * CMPLX(3.0, 4.0);
    if (creal(w) != -5.0) return 5;
    if (cimag(w) != 10.0) return 6;

    w = w / CMPLX(3.0, 4.0);
    if (creal(w) != 1.0) return 7;
    if (cimag(w) != 2.0) return 8;

    w = z + CMPLX(5.0, 6.0) - CMPLX(1.0, 1.0);
    if (creal(w) != 5.0) return 9;
    if (cimag(w) != 7.0) return 10;

    w = -z;
    if (creal(w) != -1.0) return 11;
    if (cimag(w) != -2.0) return 12;

    w = conj(z);
    if (creal(w) != 1.0) return 13;
    if (cimag(w) != -2.0) return 14;

    w = 5.0;
    if (creal(w) != 5.0) return 15;
    if (cimag(w) != 0.0) return 16;

    struct Box box;
    box.z = CMPLX(8.0, 9.0);
    if (creal(box.z) != 8.0) return 17;
    if (cimag(box.z) != 9.0) return 18;

    float complex f = CMPLXF(1.0f, 2.0f);
    if (sizeof(f) != 8) return 19;
    if (crealf(f) != 1.0f) return 20;
    if (cimagf(f) != 2.0f) return 21;

    int selected = _Generic(z, double complex: 42, default: 0);
    if (selected != 42) return 22;

    if (z != CMPLX(1.0, 2.0)) return 23;
    if (z == CMPLX(2.0, 1.0)) return 24;

    return 42;
}
