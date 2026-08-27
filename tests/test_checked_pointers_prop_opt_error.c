// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #942's OPT candidates emit CHKRO instead of CHKR and refresh their snapshot
// temps at a non-rooted store too (not just skip the refresh). This proves an
// out-of-bounds access on the checked-rooted branch of an OPT candidate
// traps.

int main(void) {
    int n                                  = 4;
    int *[[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int          other[16]                 = {0};
    volatile int c                         = 1;
    int         *q = other; // unrooted store -- makes q an OPT candidate
    if (c)
        q = p;              // rooted store -- always taken here
    volatile int i = 10;    // OOB against p's count(4)
    int          x = q[i];
    return x;
}
