// EXPECT_COMPILE_ERROR
// A bounds expression must be side-effect-free (#770/#483), and that check
// must see through a ternary's branches too (#949): node_has_side_effects()
// used to recurse into ND_COND's ->cond only, missing ->then/->els, so
// count(c ? i++ : 3) was wrongly accepted and i++ would have run on every
// checked access instead of never. See
// tests/test_checked_pointers_side_effect_bounds_error.c for the
// non-ternary case and node_has_side_effects() in src/parse.c.

int main(void) {
    int i                                               = 0;
    int c                                               = 1;
    int * [[ cccc::array, cccc::count(c ? i++ : 3) ]] a = (int[3]){1, 2, 3};
    return a[0];
}
