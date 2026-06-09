// CCCC_FLAGS: -Wconversion
// CCCC_EXPECT_STDERR: \[-Wconversion\]

int narrow(int x) {
    char c = x;  // int -> char: narrowing
    return c;
}

int main(void) {
    long big = 100000L;
    int small = big;  // long -> int: narrowing
    return narrow(small) != 100000 ? 42 : 0;
}
