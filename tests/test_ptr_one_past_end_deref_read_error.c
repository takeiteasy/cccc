// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #983: read-side mirror of test_ptr_one_past_end_deref_error.c, exercising
// CHKD's emission in emit_load_safety_checks (the store-side test only
// covers emit_store_ex).
#include <stdlib.h>
int main(void) {
    int *p = malloc(4 * sizeof(int));
    if (!p)
        return 255;
    int x = p[4]; // exactly one past the end -- must still trap
    free(p);
    return x == 0 ? 42 : 1;
}
