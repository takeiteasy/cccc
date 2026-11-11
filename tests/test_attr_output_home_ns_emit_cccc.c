// --emit-cccc keeps cccc attributes as [[cccc::...]] in any source namespace.
// CCCC_FLAGS: --emit-cccc -E
// CCCC_EXPECT_STDOUT: int \*\[\[cccc::single\]\] p;
// CCCC_EXPECT_STDOUT: int \*\[\[cccc::single\]\] q;
// CCCC_EXPECT_STDOUT: struct __attribute__\(\(packed\)\) S
// CCCC_REJECT_STDOUT: gnu::
int *[[gnu::single]] p;
int *[[single]] q;
struct [[cccc::packed]] S {
    char c;
    int  x;
};
int main(void) {
    return 42;
}
