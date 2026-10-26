// CCCC_FLAGS: --checked-pointers

static int buf[8];
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
static int sink(int *[[cccc::array, cccc::count(n)]] p, int n) {
    return p[0];
}
int main(void) {
    return sink(head(8), 8) + sink(head(8), 3) + 42;
}
