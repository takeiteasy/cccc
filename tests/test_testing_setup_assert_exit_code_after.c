// CCCC_NATIVE_SKIP: --testing=native refuses --test-timeout
// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^ok 1 - first
// CCCC_EXPECT_STDERR: ^not ok 2 - code3
// CCCC_EXPECT_STDERR: setup said no
// CCCC_EXPECT_STDERR: ^ok 3 - last
// CCCC_REJECT_STDERR: Host C crash
// A setup failure before an exit_code= test that follows an ordinary test
// fails only that test.
[[cccc::test_setup(name = "code*")]]
void setup(void) {
    AssertMsg(0, "setup said no");
}

[[cccc::test]]
void first(void) {}

[[cccc::test(exit_code = 3)]]
int code3(void) {
    return 3;
}

[[cccc::test]]
void last(void) {}
