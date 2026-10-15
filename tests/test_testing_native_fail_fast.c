// CCCC_FLAGS: --testing=native --fail-fast
// CCCC_EXPECT_STDERR: not ok 2 - second_fails
// CCCC_REJECT_STDERR: third_never
// --fail-fast stops the native harness after the first failing test.
[[cccc::test]]
void first_ok(void) {}

[[cccc::test]]
void second_fails(void) {
    Assert(0);
}

[[cccc::test]]
void third_never(void) {}
