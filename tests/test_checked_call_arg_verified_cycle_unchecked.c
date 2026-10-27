// CCCC_FLAGS: --checked-pointers
// Verified siblings that name each other stay unchecked, and each runs once.

static int a[2] = {1, 1};
static int b[2] = {1, 1};
static int calls;
static int *[[cccc::array, cccc::count(2)]] mka(void) {
    calls++;
    return (int *[[cccc::array, cccc::count(2), cccc::assume]])a;
}
static int *[[cccc::array, cccc::count(2)]] mkb(void) {
    calls++;
    return (int *[[cccc::array, cccc::count(2), cccc::assume]])b;
}
static int sink(int *[[cccc::array, cccc::count(*q)]] p,
                int *[[cccc::array, cccc::count(*p)]] q) {
    return p[0] + q[0];
}

int main(void) {
    return sink(mka(), mkb()) + 40 + (calls == 2 ? 0 : 100);
}
