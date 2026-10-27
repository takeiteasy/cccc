// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// A local holding a plain array on this path is verified against the target.
int main(void) {
    int buf[2]                              = {1, 2};
    int big[8]                              = {0};
    int *[[cccc::array, cccc::count(8)]] c8 = big;
    volatile int c                          = 1;
    int         *q                          = buf;
    if (!c)
        q = c8;
    int n                                  = 8;
    int *[[cccc::array, cccc::count(n)]] t = q;
    return t[0] + 41;
}
