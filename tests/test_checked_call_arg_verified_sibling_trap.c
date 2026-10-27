// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// A verified sibling with side effects is stored once, before the call, and the
// pointer that names it is checked against the stored value.

static int k = 8;
static int *[[cccc::single]] mk(void) {
    return (int *[[cccc::single, cccc::assume]])&k;
}
static int sink(int *[[cccc::array, cccc::count(*p)]] q,
                int *[[cccc::single]] p) {
    return q[0];
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    return sink(small, mk()) + 42;
}
