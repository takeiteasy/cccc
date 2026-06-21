// CCCC_FLAGS: --build --build-dry-run --build-cc=cc
// CCCC_EXPECT_STDOUT: cc
//
// --build-cc=COMPILER overrides the compiler binary for all targets.
// "cc" is available on any CI host.  Verified via dry-run.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
    AddSource(t, "src/main.c");
    return BuildDefault(ctx);
}
