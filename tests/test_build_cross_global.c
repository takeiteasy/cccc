// CCCC_FLAGS: --build --build-dry-run --build-triple=aarch64-linux-gnu
// CCCC_EXPECT_STDOUT: --target aarch64-linux-gnu
//
// --build-triple applies the triple to all targets (multi-target case).
// Verified via dry-run so no cross-compiler is needed.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *a = Executable(ctx, "app");
    AddSource(a, "src/main.c");
    BuildTarget *b = StaticLib(ctx, "mylib");
    AddSource(b, "src/main.c");
    return BuildDefault(ctx);
}
