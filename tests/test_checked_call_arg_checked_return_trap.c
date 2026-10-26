// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// The arguments of a checked-return call are verified when its result is
// stored into a checked pointer.

static int *[[cccc::array,
              cccc::count(n)]] mk(int *[[cccc::array, cccc::count(n)]] p,
                                  int n) {
    return p;
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    int *[[cccc::array, cccc::count(2)]] q     = mk(small, 8);
    (void)q;
    return 42;
}
