// CCCC_FLAGS: -E --attr-target=c23 --output-compiler=gcc
// CCCC_EXPECT_STDOUT: \[\[gnu::unavailable\]\] void u\(void\);
// CCCC_EXPECT_STDOUT: \[\[gnu::optimize\("O2"\)\]\] void k\(void\);
__attribute__((unavailable)) void u(void);
__attribute__((optimize("O2"))) void k(void);
int main(void) {
    return 42;
}
