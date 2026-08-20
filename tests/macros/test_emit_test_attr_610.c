// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDOUT: emitted_test_fn
// Ticket #610: [[cccc::test]] functions inside emit blocks must be registered
// and executed. The TAP "ok N - emitted_test_fn" line only appears when the
// test is both registered and run successfully.

#pragma cccc comptime begin
#pragma cccc emit begin
[[cccc::test]]
void emitted_test_fn(void) {}
#pragma cccc emit end
#pragma cccc comptime end
