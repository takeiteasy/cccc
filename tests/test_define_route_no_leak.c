// Ticket #611: @test route attribute must not leak into -G generated output.
// In testing mode (active), a #define @test is processed as a plain #define and
// the @test route token must not appear in the serialized generated output.
// CCCC_FLAGS: --testing -G
// CCCC_EXPECT_STDOUT: #define TEST_ROUTE_CANARY 99
// CCCC_REJECT_STDOUT: @test

#define @test TEST_ROUTE_CANARY 99

[[cccc::test]]
void test_route_no_leak(void) {
    AssertEq(TEST_ROUTE_CANARY, 99);
}
