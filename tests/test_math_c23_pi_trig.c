// C23 exp10 and pi-scaled trig, including the float and long double
// variants. Exact at integer and half-integer arguments.
#include <math.h>

static int near(double a, double b) {
    return fabs(a - b) < 1e-9;
}

int main(void) {
    if (exp10(2.0) != 100.0 || exp10f(1.0f) != 10.0f)
        return 1;
    if (!near((double)exp10l(3.0L), 1000.0))
        return 2;
    if (sinpi(1.0) != 0.0 || sinpi(0.5) != 1.0 || sinpif(-0.5f) != -1.0f)
        return 3;
    if (!signbit(sinpi(-2.0)))
        return 4;
    if (cospi(1.0) != -1.0 || cospif(0.5f) != 0.0f || cospil(2.0L) != 1.0L)
        return 5;
    if (tanpi(1.0) != 0.0 || !near(tanpi(0.25), 1.0))
        return 6;
    if (asinpi(1.0) != 0.5 || acospi(-1.0) != 1.0 || atanpi(1.0) != 0.25)
        return 7;
    if (atan2pi(1.0, -1.0) != 0.75 || atan2pif(-1.0f, 0.0f) != -0.5f)
        return 8;
    if (!near((double)atan2pil(1.0L, 1.0L), 0.25))
        return 9;
    return 42;
}
