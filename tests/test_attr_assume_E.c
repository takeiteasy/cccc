// CCCC_FLAGS: -E -P --emit-cccc
// CCCC_EXPECT_STDOUT: __attribute__\(\(assume\(x > 0\)\)\);
// CCCC_EXPECT_STDOUT: \[\[cccc::assume\]\]
int f(int x) {
    [[gnu::assume(x > 0)]];
    [[cccc::assume]];
    return x;
}
int main(void) {
    return 42;
}
