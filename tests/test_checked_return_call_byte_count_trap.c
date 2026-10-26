// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

static int buf[8];
static int *[[cccc::array, cccc::byte_count(n)]] bytes(int n) {
    return (int *[[cccc::array, cccc::byte_count(n), cccc::assume]])buf;
}
int main(void) {
    int *q = bytes(8);
    return q[2] + 42;
}
