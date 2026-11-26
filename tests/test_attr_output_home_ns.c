// -E prints each attribute in its home spelling. A known name in a namespace
// it lacks is printed as written, or dropped under cccc::.
// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: struct __attribute__\(\(packed\)\) S
// CCCC_EXPECT_STDOUT: int x __attribute__\(\(aligned\(16\)\)\)
// CCCC_EXPECT_STDOUT: __attribute__\(\(cold\)\) int k\(void\);
// CCCC_EXPECT_STDOUT: \[\[deprecated\]\] int f\(void\);
// CCCC_EXPECT_STDOUT: \[\[acme::foo\]\] int z;
// CCCC_EXPECT_STDOUT: \[\[acme::packed\]\] int m;
// CCCC_EXPECT_STDOUT: ^ *int n;
// CCCC_EXPECT_STDOUT: \[\[deprecated, acme::foo\(1, 2\)\]\] __attribute__\(\(packed, cold\)\) int g\(void\);
// CCCC_REJECT_STDOUT: gnu::
// CCCC_REJECT_STDOUT: cccc::
// CCCC_REJECT_STDOUT: single
struct [[gnu::packed]] S {
    char c;
    int  x;
};
struct T {
    int x [[gnu::aligned(16)]];
};
[[gnu::cold]] int k(void);
[[gnu::deprecated]] int f(void);
[[acme::foo]] int z;
[[gnu::deprecated, gnu::packed, gnu::cold, acme::foo(1, 2)]] int g(void);
int *[[cccc::single]] p;
[[acme::packed]] int m;
[[cccc::cold]] int   n;
int *[[single]] q;
int main(void) {
    return 42;
}
