// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
// #983: struct assignment lowers straight to an MCPY, bypassing
// emit_load_ex/emit_store_ex entirely (see the ND_ASSIGN struct/union arm
// in codegen.c) -- so it needs its own CHKD pair, not just the scalar
// load/store sites. A struct pointer formed one past the end of its array
// (legal to form, same as the scalar case) must still trap when a struct
// assignment dereferences it.
#include <stdlib.h>
typedef struct {
    int x, y;
} Pair;
int main(void) {
    Pair *p = malloc(2 * sizeof(Pair)); // valid indices 0..1
    if (!p)
        return 255;
    Pair src = {1, 2};
    p[2]     = src; // exactly one past the end -- struct MCPY deref must trap
    free(p);
    return 42;
}
