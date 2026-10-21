// FLT_ROUNDS agrees between the VM and -c=native.
#include <fenv.h>
#include <float.h>
int main(void) {
    if (fesetround(FE_TOWARDZERO) != 0)
        return 1;
    if (FLT_ROUNDS != 0)
        return 2;
    if (fesetround(FE_TONEAREST) != 0)
        return 3;
    if (FLT_ROUNDS != 1)
        return 4;
    return 42;
}
