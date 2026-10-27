// CCCC_FLAGS: --checked-pointers
static int k = 2;
static int calls;
static int *[[cccc::single]] mk(void) {
    calls++;
    return (int *[[cccc::single, cccc::assume]])&k;
}
static int sink(int *[[cccc::array, cccc::count(*p)]] q,
                int *[[cccc::single]] p) {
    return q[0] + q[1];
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){40, 2};
    return sink(small, mk()) + (calls == 1 ? 0 : 100);
}
