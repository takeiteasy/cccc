// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDERR: UseSystemHeaders is only supported on a CcccExecutable
//
// UseSystemHeaders is a cccc flag; a host-compiled target has no equivalent.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = Executable(ctx, "reject_system_headers");
    AddSource(app, "examples/build_demo/src/main.c");
    UseSystemHeaders(app);
    return BuildDefault(ctx);
}
