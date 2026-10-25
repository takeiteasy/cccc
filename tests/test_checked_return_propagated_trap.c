// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// A propagated (FULL) local is verified at the return.
static int *[[cccc::array,
              cccc::count(n)]] pick(int *[[cccc::array, cccc::count(3)]] src,
                                    int n) {
    int *q = src;
    return q;
}
int main(void) {
    int  arr[3] = {1, 2, 3};
    int *p      = pick(arr, 10);
    return p[0] + 41;
}
