// CCCC_FLAGS: --optimize=2
// #712 companion: the new gen_flonum_arg_to_scratch() scratch register comes
// from the same alloc_temp_reg() pool that a nested call's end-of-call
// reset_temp_regs() clears -- verify the scratch's value has already been
// moved out (FR2R/FMOV3) before that reset can matter, at an optimize level
// where the peephole/copy-prop/CSE passes are active.
#include <stdio.h>
#include <string.h>

static int chk(int n, double d) { return (n == 7 && d == 3.5) ? 1 : 0; }
static double twice(double d) { return d * 2.0; }

int main(void) {
    char buf[64];
    double a[2]; a[0] = 1.75;

    if (!chk(7, twice(a[0]))) return 1;

    snprintf(buf, sizeof(buf), "%d %f", 5, twice(a[0]));
    if (strcmp(buf, "5 3.500000") != 0) return 2;

    return 42;
}
