// Ticket #344: stray #pragma cccc suite end must error.
// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: stray #pragma cccc suite end without matching begin

[[cccc::test]]
void test_dummy(void) {}

#pragma cccc suite end
