// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// q = propagated source: the target's bounds must be implied by the source's.
int main(void) {
    int arr[3]                               = {1, 2, 3};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *q                                   = src;
    int  n                                   = 10;
    int *[[cccc::array, cccc::count(n)]] p   = arr;
    p                                        = q;
    return p[0] + 41;
}
