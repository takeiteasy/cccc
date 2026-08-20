// CCCC_FLAGS: --testing --build --fail-fast
// Suite: --fail-fast with passing tests still allows build entry to run (ticket
// #608). Source test: test_mode_compose_failfast.

#pragma cccc suite begin "compose_failfast"

[[cccc::test]]
void test_passes(void) {
    AssertEq(6 * 7, 42);
}

#pragma cccc suite end

// Build entry runs after all tests pass (fail-fast not triggered since tests
// pass).
[[cccc::build]]
int build_main(void) {
    return 0;
}
