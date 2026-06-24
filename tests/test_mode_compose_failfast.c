// CCCC_FLAGS: --testing --build --fail-fast
// #608: --fail-fast with passing tests still allows the build entry to run.
// Complementary to test_mode_compose.c — confirms fail-fast does not block
// the build when tests succeed.

[[cccc::test]]
void test_passes(void) {
    AssertEq(6 * 7, 42);
}

[[cccc::build]]
int build_main(void) {
    return 42;
}
