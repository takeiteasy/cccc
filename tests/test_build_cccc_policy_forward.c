// CCCC_FLAGS: --build --comptime-allow=exec --build-out-dir=build/test_cccc_policy_forward_out
// CCCC_EXPECT_STDOUT: build succeeded
//
// A CcccExecutable child cccc gets the build's own --comptime-allow, so its
// comptime code can run a command here and not when the build was given no
// grant. It also never reads a project config file.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = CcccExecutable(ctx, "cccc_policy_forward_app");
    AddSource(app, "tests/data/build_policy_child.c");
    return BuildDefault(ctx);
}
