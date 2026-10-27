// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// A verified pointer named by a later parameter's bound does not hide that
// argument.

static int k = 8;
static int sink(int *[[cccc::single]] p,
                int *[[cccc::array, cccc::count(*p)]] q) {
    return q[0];
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    int *[[cccc::single]] pk                   = &k;
    return sink(pk, small) + 42;
}
