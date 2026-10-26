// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// A local assigned from a checked-return call inherits the call's bounds.
static int buf[4] = {1, 2, 3, 4};
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
int main(void) {
    int *q = head(2);
    return q[3] + 41;
}
