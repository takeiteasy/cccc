// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

static int buf[8];
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
static int *[[cccc::array, cccc::count(8)]] wide(void) {
    return head(2);
}
int main(void) {
    return wide()[0] + 42;
}
