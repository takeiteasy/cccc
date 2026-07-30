// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: --target=aarch64-linux-gnu
//
// SetTargetTriple() applies --target=<triple> to a single target.
// Verified via dry-run so no cross-compiler is needed.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "cross_app");
    AddSource(t, "src/main.c");
    SetTargetTriple(t, "aarch64-linux-gnu");
    return BuildDefault(ctx);
}
