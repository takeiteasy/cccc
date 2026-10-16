// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^not ok 2 - in_body
// CCCC_EXPECT_STDERR: body said no
// CCCC_EXPECT_STDERR: ^ok 3 - last
// CCCC_REJECT_STDERR: expected exit_code
// An Assert in the body of an exit_code= test fails it; the forked child does
// not resume the harness and re-run later tests.
[[cccc::test]]
void first(void) {}

[[cccc::test(exit_code = 3)]]
int in_body(void) {
    AssertMsg(0, "body said no");
    return 3;
}

[[cccc::test]]
void last(void) {}
