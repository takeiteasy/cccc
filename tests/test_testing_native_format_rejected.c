// CCCC_FLAGS: --testing=native --test-format=json
// CCCC_EXPECT_STDERR: --testing=native only supports --test-format=tap
// EXPECT_COMPILE_ERROR
[[cccc::test]]
void t(void) {}
