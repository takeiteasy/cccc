// EXPECT_COMPILE_ERROR
// A bounds expression must be side-effect-free (#770/#483): it is
// re-evaluated at every checked access, so count(i++) would increment i on
// every dereference rather than once. See node_has_side_effects() in
// src/parse.c.

int main(void) {
    int i = 0;
    int * [[cccc::array, cccc::count(i++)]] a = (int[3]){1, 2, 3};
    return a[0];
}
