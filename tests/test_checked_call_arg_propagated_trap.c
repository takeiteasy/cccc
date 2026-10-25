// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// A propagated local passed to a checked parameter is verified.
static int take(int *[[cccc::array, cccc::count(n)]] p, int n) {
    return p[0];
}
int main(void) {
    int arr[3]                               = {1, 2, 3};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *q                                   = src;
    return take(q, 10) + 41;
}
