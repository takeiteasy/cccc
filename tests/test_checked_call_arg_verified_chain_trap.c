// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// A verified sibling whose own bound names a side-effecting argument is stored
// after that argument.

static int buf[8] = {8};
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
    return q[0];
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    return sink(small, mk(), four()) + 42;
}
