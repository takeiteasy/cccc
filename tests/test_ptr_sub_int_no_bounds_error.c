// CCCC_FLAGS: -2
// #982 (defect B): CHKB unconditionally added its scaled offset to the
// base pointer's own offset into its allocation -- correct for `p + n`,
// but new_sub hands CHKB a *positive* scaled magnitude for `p - n` too, so
// `p - n` was checked as `base_off + n` instead of `base_off - n`. Any
// `p - n` whose true (correct) result was safely inside the allocation
// could still trip a false ARRAY BOUNDS ERROR once `n` was large enough
// that `base_off + n` (the wrong direction) sailed past the allocation's
// own size. General CHKB bug, not VLA-specific -- verified against a plain
// malloc block (see tests/suites/test_suite_vla.c's
// test_vla_row_ptr_minus_num for the VLA row-pointer repro that surfaced
// it). Fixed by a new CHKBN opcode (subtracting form) emitted for ND_SUB
// pointer arithmetic instead of reusing CHKB's adding form.
#include <stdlib.h>
int main(void) {
    char *p = malloc(16);
    if (!p)
        return 1;
    // Deliberately interior on both ends (not one-past-the-end) -- pointer
    // FORMATION landing exactly at the allocation's boundary is a separate,
    // deliberately deferred issue (#982's defect C, a new follow-up
    // ticket), not what this test is about.
    char *q = p + 12;
    char *r = q - 8;   // steps back to p+4, well inside the allocation
    *r = 42;
    int v = (int)*r;
    free(p);
    return v == 42 ? 42 : 1;
}
