// CCCC_FLAGS: --testing=native --list-tests
// CCCC_EXPECT_STDOUT: # Tests \(2 total\)
// CCCC_EXPECT_STDOUT: first
// CCCC_EXPECT_STDOUT: second
// CCCC_REJECT_STDOUT: ok 1
// --list-tests prints the tests without running them.
[[cccc::test]]
void first(void) {
    Assert(0);
}

[[cccc::test]]
void second(void) {
    Assert(0);
}
