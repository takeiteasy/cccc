// CCCC_FLAGS: --testing=native --test-timeout=1
// CCCC_EXPECT_STDERR: not ok 1 - hangs
// CCCC_EXPECT_STDERR: TIMEOUT
// --test-timeout=N (seconds) kills a native test that runs too long.
[[cccc::test]]
void hangs(void) {
    volatile int spin = 1;
    while (spin) {
    }
}
