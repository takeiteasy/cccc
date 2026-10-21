// -c=native, a _Complex global with a non-zero imaginary part folds and
// round-trips
#include <complex.h>
static double _Complex zc = 3.0 + 4.0 * I;
static float _Complex zf  = 1.5f + 2.5f * I;
static double _Complex zm = (2.0 + 1.0 * I) * (3.0 + 4.0 * I);
int main(void) {
    double _Complex lm = (2.0 + 1.0 * I) * (3.0 + 4.0 * I);
    if (creal(lm) != creal(zm) || cimag(lm) != cimag(zm))
        return 1;
    return (int)creal(zc) + (int)cimag(zc) + (int)crealf(zf) + (int)cimagf(zf) +
                       (int)creal(zm) + (int)cimag(zm) ==
                   3 + 4 + 1 + 2 + 2 + 11
               ? 42
               : 1;
}
