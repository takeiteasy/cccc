// CCCC_FLAGS: --testing=native --test-format=plain
// CCCC_EXPECT_STDERR: Running 3 tests
// CCCC_EXPECT_STDERR: ✓ passes
// CCCC_EXPECT_STDERR: ── math ──
// CCCC_EXPECT_STDERR: ✗ fails \(
// CCCC_EXPECT_STDERR: Failed: +1
// CCCC_REJECT_STDERR: TAP version
// --test-format=plain prints the VM runner's plain reporter.
[[cccc::test]]
void passes(void) {}

#pragma cccc suite begin "math"
[[cccc::test]]
void adds(void) {}

[[cccc::test]]
void fails(void) {
    Assert(0);
}
#pragma cccc suite end
