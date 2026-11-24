// --emit-cccc keeps cccc attributes, and any name written under cccc::, as
// [[cccc::...]].
// CCCC_FLAGS: --emit-cccc -E
// CCCC_EXPECT_STDOUT: int \*\[\[cccc::single\]\] p;
// CCCC_EXPECT_STDOUT: int \*\[\[cccc::single\]\] q;
// CCCC_EXPECT_STDOUT: struct __attribute__\(\(packed\)\) S
// CCCC_EXPECT_STDOUT: \[\[cccc::cold\]\] int n;
// CCCC_REJECT_STDOUT: gnu::
int *[[cccc::single]] p;
int *[[single]] q;
struct [[gnu::packed]] S {
    char c;
    int  x;
};
[[cccc::cold]] int n;
int main(void) {
    return 42;
}
