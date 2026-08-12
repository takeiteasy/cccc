// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #983: the load-bearing test for the formation-vs-dereference split.
// Forming `p + 4` (see test_ptr_one_past_end_form.c) is now legal, but
// *dereferencing* it -- via a store, here -- must still trap: `CHKB` used
// to be the only runtime check on a subscript at all (`a[i]` desugars to
// `*(a+i)`), so relaxing it alone would have silently stopped this genuine
// out-of-bounds write from being caught. Caught by the new CHKD opcode,
// emitted at the store site.
#include <stdlib.h>
int main(void) {
    int *p = malloc(4 * sizeof(int));   // valid indices 0..3
    if (!p)
        return 255;
    p[4] = 1;   // exactly one past the end -- must still trap
    free(p);
    return 42;
}
