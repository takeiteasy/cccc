// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #486: an [[cccc::assume]] cast takes the claimed bounds on trust -- no
// check of the CLAIM itself -- but every access made THROUGH the result is
// still an ordinary declared-checked access, CHKR-checked against that
// claim like any other checked pointer. Proves CHKR reaches the desugared
// temp: the claim here (count(n), n=5) is honest, but the access at index
// 10 is genuinely out of it.

int main(void) {
    int  arr[5] = {1, 2, 3, 4, 5};
    int *raw    = arr;
    int  n      = 5;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::assume]])raw;
    volatile int i = 10;
    return p[i];
}
