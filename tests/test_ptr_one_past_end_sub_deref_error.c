// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #983: CHKBN mirror of test_ptr_one_past_end_deref_error.c. `p - n` with a
// negative `n` computes CHKBN's `eff = base_off - scaled_offset` where
// scaled_offset is itself negative (new_sub multiplies `n` by the element
// size but does not take its absolute value), so `base_off - (-magnitude)`
// == `base_off + magnitude` -- the same one-past-the-end landing spot as
// `p + 4`, but reached through the SUBTRACTING opcode (CHKBN) instead of
// CHKB. Forming it is legal C (same reasoning as the CHKB case); the
// dereference through the formed pointer must still trap via CHKD.
#include <stdlib.h>
int main(void) {
    int *p = malloc(4 * sizeof(int)); // valid indices 0..3
    if (!p)
        return 255;
    int  n = -4;
    int *q = p - n; // p - (-4) == p + 4, one past the end -- legal to form
    q[0]   = 1;     // dereferencing it -- must still trap
    free(p);
    return 42;
}
