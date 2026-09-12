// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #488: a call through a function-pointer value carries the exact same
// checked-parameter Type list as a direct call -- funcall() already
// unwraps a TY_PTR-to-TY_FUNC to the underlying TY_FUNC before this
// rewrite runs (the same `ty` computation every other per-argument check
// in funcall() uses), so rewrite_checked_call_args() needs no
// function-pointer-specific handling at all.

typedef void (*fp)(int *[[cccc::array, cccc::count(n)]] p, int n);

void sink(int *p, int n) {
    (void)p;
    (void)n;
}

int main(void) {
    fp f                                       = sink;
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    f(small, 8);
    return 42;
}
