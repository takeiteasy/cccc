// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// The returned bounds don't cover the return type's count(n).
static int *[[cccc::array,
              cccc::count(n)]] pick(int *[[cccc::array, cccc::count(3)]] src,
                                    int n) {
    return src;
}
int main(void) {
    int  arr[3] = {1, 2, 3};
    int *p      = pick(arr, 10);
    return p[0] + 41;
}
