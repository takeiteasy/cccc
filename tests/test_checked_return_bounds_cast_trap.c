// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// A bounds cast returned from a checked-return function is verified.

int *[[cccc::array, cccc::count(n)]] g(int *raw, int n) {
    return (int *[[cccc::array, cccc::count(5), cccc::assume]])raw;
}

int main(void) {
    int buf[5] = {0};
    g(buf, 100);
    return 42;
}
