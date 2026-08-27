// CCCC_FLAGS: --testing --uninitialized-detection
// CCCC_EXPECT_STDOUT: ok 1 -
// CCCC_REJECT_STDOUT: not ok
//
// Positive control for #1013: a clean, fully-initialized [[cccc::test]]
// body under --uninitialized-detection must still report "ok" and exit 0
// -- proving the new vm->runtime_fault check doesn't fail every test
// wholesale, only ones that actually hit a runtime safety violation.
[[cccc::test]]
void test_no_safety_violation(void) {
    int x = 5;
    AssertEq(x, 5);
}
