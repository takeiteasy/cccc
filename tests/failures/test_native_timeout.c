// Native mode timeout: a test with an infinite loop should be killed and
// reported as TIMEOUT when a per-test timeout is set.
// Run with --testing; expected exit code 1 (one test times out).
// CCCC_FLAGS: --testing

[[cccc::test(mode = "native", timeout = 100)]]
void test_native_infinite_loop(void) {
    for (;;) {}
}
