// CCCC_FLAGS: --build --build-dry-run --build-profile=debug
// CCCC_EXPECT_STDOUT: -g
// CCCC_EXPECT_STDOUT: -O0
//
// --build-profile=debug prepends -g and -O0 to each target's compile commands.
// Verified via dry-run so no real compilation is needed.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
    AddSource(t, "src/main.c");
    return BuildDefault(ctx);
}
