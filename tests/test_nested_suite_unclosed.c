// Ticket #344: unclosed #pragma cccc suite begin at EOF must error.
// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: unclosed #pragma cccc suite begin

#pragma cccc suite begin "never_closed"

[[cccc::test]]
void test_dummy_unclosed(void) { }

// No matching #pragma cccc suite end — should produce a compile error.
