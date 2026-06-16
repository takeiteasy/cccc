// EXPECT_COMPILE_ERROR
// Unknown flags in flags= "..." are a hard error at parse time (ticket #356).
// CCCC_FLAGS: --testing
[[cccc::test(flags = "--nonsense-flag")]]
void test_with_bad_flag(void) {}
