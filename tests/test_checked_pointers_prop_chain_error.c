// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #941's chained propagation: a chained candidate's snapshot store is a
// hidden-temp-to-hidden-temp pointer copy (`r_lo = q_lo`, not
// `q_lo = (char*)(expr over p, n)`). This proves an out-of-bounds access
// through a 3-deep propagation chain traps.

int main(void) {
    int n                                  = 4;
    int *[[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int         *q                         = p + 0;
    int         *r                         = q + 1;
    int         *s                         = r + 1;
    volatile int i = 2; // p[4] -- one past the end, through the 3-hop chain
    int          x = s[i];
    return x;
}
