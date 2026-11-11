// -E prints each attribute in its home namespace, whatever it was written in.
// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: struct __attribute__\(\(packed\)\) S
// CCCC_EXPECT_STDOUT: int x __attribute__\(\(aligned\(16\)\)\)
// CCCC_EXPECT_STDOUT: __attribute__\(\(cold\)\) int k\(void\);
// CCCC_EXPECT_STDOUT: \[\[nodiscard\]\] int f\(void\);
// CCCC_EXPECT_STDOUT: \[\[acme::foo\]\] int z;
// CCCC_EXPECT_STDOUT: \[\[nodiscard, acme::foo\(1, 2\)\]\] __attribute__\(\(packed, cold\)\) int g\(void\);
// CCCC_REJECT_STDOUT: gnu::
// CCCC_REJECT_STDOUT: cccc::
// CCCC_REJECT_STDOUT: single
struct [[cccc::packed]] S {
    char c;
    int  x;
};
struct T {
    int x [[cccc::aligned(16)]];
};
[[clang::cold]] int k(void);
[[gnu::nodiscard]] int f(void);
[[acme::foo]] int z;
[[gnu::nodiscard, cccc::packed, gnu::cold, acme::foo(1, 2)]] int g(void);
int *[[gnu::single]] p;
int *[[single]] q;
int main(void) {
    return 42;
}
