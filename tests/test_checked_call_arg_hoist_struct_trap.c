// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// A side-effecting struct sibling named by a bound is hoisted, so the call is
// checked.

struct Len {
    int n;
};

static struct Len mk(void) {
    struct Len l = {8};
    return l;
}
static int sink(int *[[cccc::array, cccc::count(l.n)]] p, struct Len l) {
    return p[0];
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    return sink(small, mk()) + 42;
}
