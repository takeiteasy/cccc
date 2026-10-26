// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

static int buf[8];
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
int main(void) {
    int *[[cccc::array, cccc::count(8)]] p =
        (int *[[cccc::array, cccc::count(8), cccc::dynamic]])head(2);
    return p[0] + 42;
}
