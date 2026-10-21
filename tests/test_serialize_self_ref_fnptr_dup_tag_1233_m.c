// CCCC_FLAGS: -m tests/fixtures/self_ref_1233_smoke_a.c tests/fixtures/self_ref_1233_smoke_b.c
// CCCC_EXPECT_STDOUT: int main
// CCCC_REJECT_STDOUT: __cccc_dup
// -m output does not rename a tag both TUs complete identically.
#include "fixtures/self_ref_1233_smoke.h"
SelfRef1233 *make_node_1233_a(int val);
SelfRef1233 *make_node_1233_b(int val);
int call_fn_1233_a(SelfRef1233 *n);
int call_fn_1233_b(SelfRef1233 *n);
int chain_1233(SelfRef1233 *a, SelfRef1233 *b);
int main(void) {
    SelfRef1233 *a = make_node_1233_a(10);
    SelfRef1233 *b = make_node_1233_b(7);
    if (call_fn_1233_a(a) != 20)
        return 1;
    if (call_fn_1233_b(b) != 21)
        return 2;
    if (chain_1233(a, b) != 7)
        return 3;
    return 42;
}
