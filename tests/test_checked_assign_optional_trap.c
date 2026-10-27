// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// An OPT source rooted on this path is verified against the target's bounds.
int main(void) {
    int arr[10]                              = {1, 2, 40};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *raw                                 = arr;
    int  c                                   = 1;
    int *q                                   = raw;
    if (c)
        q = src;
    int n                                  = 10;
    int *[[cccc::array, cccc::count(n)]] p = arr;
    p                                      = q;
    return p[2] + 2;
}
