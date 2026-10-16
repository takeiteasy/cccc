// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^ok 2 - code3
// CCCC_EXPECT_STDERR: ^not ok 1 - bad
// An exit_code= test still runs after an earlier test failed an Assert.
[[cccc::test]]
void bad(void) {
    Assert(0);
}

[[cccc::test(exit_code = 3)]]
int code3(void) {
    return 3;
}
