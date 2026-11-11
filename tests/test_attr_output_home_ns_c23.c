// CCCC_FLAGS: --attr-target=c23 -E
// CCCC_EXPECT_STDOUT: struct \[\[gnu::packed\]\] S
// CCCC_EXPECT_STDOUT: \[\[gnu::noinline\]\] int f\(void\);
// CCCC_EXPECT_STDOUT: \[\[nodiscard, gnu::packed, gnu::cold, acme::foo\(1, 2\)\]\] int g\(void\);
// CCCC_REJECT_STDOUT: __attribute__
// CCCC_REJECT_STDOUT: cccc::
struct __attribute__((packed)) S {
    char c;
    int  x;
};
__attribute__((noinline)) int f(void);
[[gnu::nodiscard, cccc::packed, gnu::cold, acme::foo(1, 2)]] int g(void);
int *[[single]] p;
int main(void) {
    return 42;
}
