// EXPECT_COMPILE_ERROR CCCC_FLAGS: --checked-pointers
// q's address escapes, so its bounds can't be trusted.

int main(void) {
    int arr[3]                               = {1, 2, 40};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int  *q                                  = src;
    int **pp                                 = &q;
    (void)pp;
    int n = 3;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::dynamic]])q;
    return p[0];
}
