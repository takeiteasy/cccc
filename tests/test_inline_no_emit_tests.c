// CCCC_FLAGS: --testing --testing=native --no-emit-tests
// CCCC_EXPECT_STDERR: --no-emit-tests is incompatible with --testing=native
// Not run natively: already IS a --testing=native invocation (that's the
// point of the test); the native pass's bare "--testing" ->
// "--testing=native" rewrite would be redundant with what this file
// already exercises directly.
//
// Negative test: --no-emit-tests drops [[cccc::test]] bodies from
// native/generated output, which would leave --testing=native's generated
// harness with nothing to run -- CCCC must refuse the combination outright
// rather than silently emitting an empty (fork-per-test, i.e. no-op) suite.
// (Bare --testing ahead of --testing=native is redundant on the CLI --
// getopt just applies the backend from the last occurrence -- but it makes
// the test driver's own is_testing_mode check, which only recognizes an
// exact "--testing" token, see this as the testing-mode CLI test it is.)

@test void t(void) {
    AssertTrue(1);
}
