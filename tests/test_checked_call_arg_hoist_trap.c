// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// A side-effecting sibling argument is hoisted, so the call is still checked.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

static int f(void) {
    return 8;
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    sink(small, f());
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
