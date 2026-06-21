// CCCC_FLAGS: --build --build-dry-run --build-profile=release
// CCCC_EXPECT_STDOUT: -O2
// CCCC_EXPECT_STDOUT: -DNDEBUG
//
// --build-profile=release prepends -O2 and -DNDEBUG.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
    AddSource(t, "src/main.c");
    return BuildDefault(ctx);
}
