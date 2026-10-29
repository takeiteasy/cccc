// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: -F fw/host
// CCCC_EXPECT_STDOUT: --iframework fw/self
// CCCC_EXPECT_STDOUT: --use-system-headers
//
// AddFrameworkPath is -F on a host target (compile and link) and --iframework
// on a CcccExecutable.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *host = Executable(ctx, "fw_host");
    AddSource(host, "examples/build_demo/src/main.c");
    AddFrameworkPath(host, "fw/host");

    BuildTarget *self = CcccExecutable(ctx, "fw_cccc");
    AddSource(self, "examples/build_demo/src/main.c");
    AddFrameworkPath(self, "fw/self");
    UseSystemHeaders(self);
    return BuildDefault(ctx);
}
