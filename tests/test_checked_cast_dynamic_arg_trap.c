// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// A dynamic cast over-claim used as a call argument traps.
static int take(int *p) {
    return p[0];
}
int main(void) {
    int arr[3]                               = {1, 2, 3};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int n                                    = 10;
    return take((int *[[cccc::array, cccc::count(n), cccc::dynamic]])src) + 41;
}
