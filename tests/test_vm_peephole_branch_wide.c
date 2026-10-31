// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: BLTW3
// CCCC_EXPECT_STDOUT: B(EQ|NE)W3
// CCCC_EXPECT_STDOUT: BUGTW3|BULEW3
// CCCC_EXPECT_STDOUT: SLTI3 +[0-9]+ 10000000
// A compare against a constant too wide for the 24-bit immediate is fused into
// one branch with a 56-bit immediate, except where an instruction sits between
// the compare and its jump.
#define LIMIT 10000000

int sieve_like(char *flags) {
    int n = 0;
    for (long long i = 2; i < LIMIT; i++)
        n += flags[i & 7] == 0;
    return n;
}

int mask(unsigned v) {
    if (v == 4294967295u)
        return 1;
    return 0;
}

int bound(unsigned long long v) {
    return v > 36028797018963967ULL ? 1 : 0;
}

int stored(long long a, int *p) {
    int r = a < LIMIT;
    *p    = 1;
    if (r)
        return 1;
    return 0;
}

int main(void) {
    char flags[8]  = {0, 0, 0, 0, 0, 0, 0, 0};
    int  p         = 0;
    int  ok        = sieve_like(flags) == LIMIT - 2;
    ok            &= mask(4294967295u) == 1 && mask(4294967294u) == 0;
    ok &= bound(36028797018963967ULL) == 0 && bound(36028797018963968ULL) == 1;
    ok &= stored(5, &p) == 1 && p == 1;
    p   = 0;
    ok &= stored(LIMIT, &p) == 0 && p == 1;
    return ok ? 42 : 1;
}
