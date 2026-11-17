// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: custom step 'wedged' timed out after 1 second
//
// StepTimeout caps a RunCustom step's wall-clock time. The step runs in its
// own process group, so the command and anything it spawned are killed and the
// build fails instead of waiting for the command to finish on its own.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *step = RunCustom(ctx, "wedged", "sleep 60");
    StepTimeout(step, 1);
    return BuildDefault(ctx);
}
