// CCCC_FLAGS: --build --build-dry-run --build-triple=aarch64-linux-gnu
// CCCC_EXPECT_STDOUT: --target=aarch64-linux-gnu
//
// --build-triple=TRIPLE appends --target=<triple> to compile and link
// invocations.  Verified via dry-run so no cross-compiler is needed.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
    AddSource(t, "src/main.c");
    return BuildDefault(ctx);
}
