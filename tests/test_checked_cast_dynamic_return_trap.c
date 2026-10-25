// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// A dynamic cast over-claim in a return statement traps.
static int *[[cccc::array,
              cccc::count(10)]] get(int *[[cccc::array, cccc::count(3)]] src) {
    int n = 10;
    return (int *[[cccc::array, cccc::count(n), cccc::dynamic]])src;
}
int main(void) {
    int  arr[3] = {1, 2, 3};
    int *p      = get(arr);
    return p[0] + 41;
}
