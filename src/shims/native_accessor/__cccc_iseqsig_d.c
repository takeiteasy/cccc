#include <fenv.h>
static int __cccc_iseqsig_d(double x, double y) {
    union { double d; unsigned long long u; } __vx, __vy; __vx.d = x; __vy.d = y;
    unsigned long long ux = __vx.u, uy = __vy.u;
    int sx = ((ux & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (ux & 0x0007FFFFFFFFFFFFULL) != 0 && !(ux & 0x0008000000000000ULL);
    int sy = ((uy & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (uy & 0x0007FFFFFFFFFFFFULL) != 0 && !(uy & 0x0008000000000000ULL);
    if (sx || sy) feraiseexcept(FE_INVALID);
    return x == y;
}
