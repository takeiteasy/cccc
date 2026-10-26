// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// A dereference through a call result is checked against the callee's
// return bounds, substituted with the call's arguments.
static int buf[4] = {1, 2, 3, 4};
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
int main(void) {
    return head(2)[3] + 41;
}
