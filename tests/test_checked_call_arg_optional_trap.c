// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// An OPT local rooted on this path is verified when passed to a checked
// parameter.
static int take(int *[[cccc::array, cccc::count(n)]] p, int n) {
    return p[0];
}
int main(void) {
    int arr[10]                              = {1, 2, 3};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *raw                                 = arr;
    int  c                                   = 1;
    int *q                                   = raw;
    if (c)
        q = src;
    return take(q, 10) + 41;
}
