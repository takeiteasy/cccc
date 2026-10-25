// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// r + 3 elements runs past src's bounds.

int main(void) {
    int arr[3]                               = {1, 2, 40};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *q                                   = src;
    int *r                                   = q + 1;
    int  n                                   = 3;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::dynamic]])r;
    return p[0] + 40;
}
