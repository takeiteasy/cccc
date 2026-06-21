// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: custom step 'mustfail' failed
//
// #544: A RunCustom step that exits with a non-zero code must fail the build.
// Verified by checking that the expected diagnostic appears on stderr and that
// the build exits with a non-zero code (treated as a negative_pass by the
// test runner when CCCC_EXPECT_STDERR is set in --build mode).
//
// This test guards the exit-code patch in the vendored paul_shell.h — before
// that patch, command_execute discarded the child exit status and the custom
// step would spuriously report success.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *step = RunCustom(ctx, "mustfail", "false");
    return BuildDefault(ctx);
}
