#include <fenv.h>
static int __cccc_iseqsig_f(float x, float y) {
    union { float f; unsigned int u; } __vx, __vy; __vx.f = x; __vy.f = y;
    unsigned int ux = __vx.u, uy = __vy.u;
    int sx = ((ux & 0x7F800000U) == 0x7F800000U) && (ux & 0x003FFFFFU) != 0 && !(ux & 0x00400000U);
    int sy = ((uy & 0x7F800000U) == 0x7F800000U) && (uy & 0x003FFFFFU) != 0 && !(uy & 0x00400000U);
    if (sx || sy) feraiseexcept(FE_INVALID);
    return x == y;
}
