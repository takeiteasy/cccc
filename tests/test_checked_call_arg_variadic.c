// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #488: an annotated FIXED parameter ahead of a variadic `...` tail is
// checked exactly like an ordinary call; the variadic arguments
// themselves have no param_ty at all, so rewrite_checked_call_args()'s
// paired walk (Type *p, Node *a) simply runs out of `p` and stops,
// leaving them untouched -- no special-casing needed. `small` (count(2))
// is passed where `sink`'s first parameter claims count(8); the trailing
// variadic ints are along for the ride.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n, ...);

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    sink(small, 8, 1, 2, 3);
    return 42;
}

void sink(int *p, int n, ...) {
    (void)p;
    (void)n;
}
