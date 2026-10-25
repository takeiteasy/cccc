// EXPECT_COMPILE_ERROR CCCC_FLAGS: --checked-pointers
// q has bounds only when the branch ran, so the claim can't be verified.

int main(void) {
    int arr[3]                               = {1, 2, 40};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *raw                                 = arr;
    int  c                                   = 1;
    int *q                                   = raw;
    if (c)
        q = src;
    int n = 3;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::dynamic]])q;
    return p[0];
}
