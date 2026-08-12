// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #982 positive control for CHKBN (the new SUB form) -- the most important
// control in this set, since CHKBN is brand-new VM code: a permissive
// negation bug would stop every `p - n` bounds violation from being
// detected while leaving the rest of the suite green, exactly the failure
// mode tests/test_malloc_leak_still_reported.c guards against for #979's
// leak-detection change.
//
// This traps only on the FIXED binary: before #982, CHKB's unconditional
// add computed `eff = base_off(4) + scaled_offset(8) = 12`, which is
// (wrongly) in-bounds for a 16-byte allocation, so the real violation
// below went undetected pre-fix. CHKBN's subtracting form correctly
// computes `eff = 4 - 8 = -4`, which is out of bounds.
#include <stdlib.h>
int main(void) {
    char *p = malloc(16);
    if (!p)
        return 255;
    char *q = p + 4;
    char *r = q - 8;   // steps to p-4 -- before the allocation start
    *r = 1;             // must trap
    free(p);
    return 42;
}
