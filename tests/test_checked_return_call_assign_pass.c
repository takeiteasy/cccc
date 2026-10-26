// CCCC_FLAGS: --checked-pointers

static int buf[8];
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
int main(void) {
    int *[[cccc::array, cccc::count(8)]] p = head(8);
    int *[[cccc::array, cccc::count(2)]] q = head(8);
    return p[0] + q[1] + 42;
}
