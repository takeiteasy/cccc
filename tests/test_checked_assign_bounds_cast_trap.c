// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// A bounds cast assigned to a checked pointer is verified against its bounds.

int main(void) {
    int buf[5] = {0};
    int *raw   = buf;
    int *[[cccc::array, cccc::count(100)]] q = 0;
    q = (int *[[cccc::array, cccc::count(5), cccc::assume]])raw;
    (void)q;
    return 42;
}
