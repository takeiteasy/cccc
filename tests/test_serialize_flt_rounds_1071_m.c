// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: fegetround
// CCCC_REJECT_STDOUT: __builtin_flt_rounds
// -m output implements FLT_ROUNDS with fegetround, not a compiler builtin.
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
