// CCCC_FLAGS: --checked-pointers

static int buf[8];
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
int main(void) {
    int *[[cccc::array, cccc::count(8)]] p =
        (int *[[cccc::array, cccc::count(8), cccc::dynamic]])head(8);
    int *q = head(8);
    int *[[cccc::array, cccc::count(4)]] r =
        (int *[[cccc::array, cccc::count(4), cccc::dynamic]])q;
    return p[0] + r[3] + 42;
}
