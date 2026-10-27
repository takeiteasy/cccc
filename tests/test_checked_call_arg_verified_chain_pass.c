// CCCC_FLAGS: --checked-pointers
static int buf[8] = {2, 0, 0, 0, 0, 0, 0, 0};
static int calls;
static int *[[cccc::array, cccc::count(8)]] mk(void) {
    calls++;
    return (int *[[cccc::array, cccc::count(8), cccc::assume]])buf;
}
static int four(void) {
    calls++;
    return 4;
}
static int sink(int *[[cccc::array, cccc::count(*p)]] q,
                int *[[cccc::array, cccc::count(n)]] p, int n) {
    return q[0] + q[1] + n;
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){30, 8};
    return sink(small, mk(), four()) + (calls == 2 ? 0 : 100);
}
