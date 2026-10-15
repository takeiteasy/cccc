// CCCC_FLAGS: --testing --test-format=plain --test-timeout=1
// CCCC_EXPECT_STDERR: ✗ hangs \(TIMEOUT\)
// A plain-format timeout reads `✗ name (TIMEOUT)`.
[[cccc::test]]
void hangs(void) {
    volatile int spin = 1;
    while (spin) {
    }
}
