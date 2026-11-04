// CCCC_FLAGS: -m --checked-pointers
// CCCC_REJECT_STDERR: ignores VM runtime safety/debug options
// CCCC_EXPECT_STDOUT: (__cccc_chkr\(\([a-z].*){3}
// CCCC_REJECT_STDOUT: (__cccc_chkr\(\([a-z].*){4}
// CCCC_EXPECT_STDOUT: __cccc_chknt\([^c]
//
// -m emits one lowered range check for a read-modify-write, for a repeated
// access and for an access repeated across an `if`, and the terminator-slot
// guard of a store stays.

int rmw(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    p[i] += 3;
    return p[i];
}

int join(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = p[i];
    if (c)
        x += p[i];
    return x + p[i];
}

int main(void) {
    int    a[4]            = {1, 2, 3, 4};
    char s _Nt_checked[4]  = "abc";
    int    n               = 2;
    s[n]                  += 1;
    return rmw(a, 4, 0) == 4 && join(a, 4, 1, 1) == 6 && s[2] == 'd' ? 42 : 1;
}
