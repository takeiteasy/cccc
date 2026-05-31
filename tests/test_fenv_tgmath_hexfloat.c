#include <fenv.h>
#include <math.h>
#include <tgmath.h>

int main(void) {
    double x = 0x1.8p+1;
    if (x != 3.0) return 1;

    if (feclearexcept(FE_ALL_EXCEPT) != 0) return 2;
    if (fetestexcept(FE_ALL_EXCEPT) != 0) return 3;

    float f = -3.0f;
    double d = -4.0;
    if (fabs(f) != 3.0f) return 4;
    if (sqrt(d * d) != 4.0) return 5;

    return 42;
}
