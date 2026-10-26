// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// The callee shrinks n before returning a 3-element array; the return is
// verified against the n it was called with, which the caller trusts.
static int buf[8];
static int *[[cccc::array,
              cccc::count(n)]] head(int *[[cccc::array, cccc::count(3)]] src,
                                    int n) {
    n = 3;
    return src;
}
int main(void) {
    int  arr[3] = {1, 2, 3};
    int *p      = head(arr, 10);
    return p[0] + 41;
}
