// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O2
// CCCC_MATRIX_SKIP: depends on -O2 specifically; the matrix only ever runs -O0 + one -f pass.
// #921's member-rooted deref (s.p[i]) reaches its address through an
// ND_MEMBER chain rather than a bare ND_VAR -- test_checked_pointers_
// fusion_o2.c only proves the variable-rooted case survives -O2's indexed
// load/store fusion (match_indexed_addr() in src/codegen.c), which would
// not have caught a regression specific to the member path. This proves
// the out-of-bounds access through a member still traps at -O2.

struct S {
    int n;
    int * [[cccc::array, cccc::count(n)]] p;
};

int main(void) {
    struct S s = {4, (int[4]){1, 2, 3, 4}};
    volatile int i = 4;
    int x = s.p[i];
    return x;
}
