// CCCC_FLAGS: --build --build-out-dir=build/test_cccc_policy_denied_out
// CCCC_EXPECT_STDOUT: build failed \(1 error\)
// CCCC_EXPECT_STDERR: comptime call to .system. denied
//
// Without --comptime-allow on the build, a CcccExecutable child's comptime
// code cannot run a command (the allowed counterpart is
// test_build_cccc_policy_forward).

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = CcccExecutable(ctx, "cccc_policy_denied_app");
    AddSource(app, "tests/data/build_policy_child.c");
    return BuildDefault(ctx);
}
