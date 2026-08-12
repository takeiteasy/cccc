// CCCC_FLAGS: -2
// #983: forming a pointer exactly one past the end of its allocation is
// legal C (only dereferencing it is undefined) -- e.g. `p + 4` on a
// 4-element allocation. Before #983, CHKB's `eff >= header->size` bounds
// test rejected forming such a pointer at all, false-positiving on
// perfectly legal C. Fixed by relaxing chkb_common's formation-time check
// to `eff > header->size` and adding a separate CHKD check at the
// dereference site (see test_ptr_one_past_end_deref_error.c for proof the
// dereference is still caught).
#include <stdlib.h>
int main(void) {
    int *p = malloc(4 * sizeof(int));   // valid indices 0..3
    if (!p)
        return 255;
    int *e = p + 4;   // one-past-the-end -- legal to FORM, must not trap
    long d = e - p;
    free(p);
    return d == 4 ? 42 : 1;
}
