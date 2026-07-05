// Ticket #611: @test route on #define, #ifdef in testing mode (active).
// CCCC_FLAGS: --testing

// @test defines are applied in testing mode.
#define @test  TEST_TIMEOUT_MS 5000
// @build defines are NOT applied in testing mode.
#define @build BUILD_ONLY 99

[[cccc::test]]
void test_at_test_define(void) {
    // @test define must be present.
    AssertEq(TEST_TIMEOUT_MS, 5000);

    // @build define must be absent.
#ifdef BUILD_ONLY
    Assert(0); // must not execute
#endif
}

[[cccc::test]]
void test_at_test_ifdef(void) {
    // @test #ifdef must be true for TEST_TIMEOUT_MS.
    int seen = 0;
#ifdef @test TEST_TIMEOUT_MS
    seen = 1;
#endif
    AssertEq(seen, 1);

    // @build #ifdef must be false (mode inactive).
#ifdef @build BUILD_ONLY
    Assert(0); // must not execute
#else
    seen = 2;
#endif
    AssertEq(seen, 2);
}
