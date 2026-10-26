// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// A dynamic cast of an assume cast is verified against the inner claim.

int main(void) {
    int buf[5] = {0};
    int *raw   = buf;
    int *[[cccc::array, cccc::count(100)]] q =
        (int *[[cccc::array, cccc::count(100), cccc::dynamic]])(
            int *[[cccc::array, cccc::count(5), cccc::assume]])raw;
    (void)q;
    return 42;
}
