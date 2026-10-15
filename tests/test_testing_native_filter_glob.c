// CCCC_FLAGS: --testing=native --test=keep_*
// CCCC_EXPECT_STDOUT: 1\.\.2
// CCCC_EXPECT_STDOUT: ok 2 - keep_b
// CCCC_REJECT_STDOUT: drop_c
// --test=GLOB selects tests by name under --testing=native.
[[cccc::test]]
void keep_a(void) {}

[[cccc::test]]
void drop_c(void) {
    Assert(0);
}

[[cccc::test]]
void keep_b(void) {}
