// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// The call's two elements do not cover the target's declared eight.
static int buf[8];
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
int main(void) {
    int *[[cccc::array, cccc::count(8)]] p = head(2);
    return p[0] + 42;
}
