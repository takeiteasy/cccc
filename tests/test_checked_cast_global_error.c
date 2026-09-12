// EXPECT_COMPILE_ERROR CCCC_FLAGS: --checked-pointers
// #486: a checked bounds cast is only valid inside a function body --
// checked_cast_desugar() (src/parse_checked.c) needs vm->compiler.current_fn
// to attach its compiler-generated local to, which is NULL for a global
// initializer.

int arr[5] = {1, 2, 3, 4, 5};
int n      = 5;
int *[[cccc::array, cccc::count(n)]] p =
    (int *[[cccc::array, cccc::count(n), cccc::assume]])arr;

int main(void) {
    return p[0];
}
