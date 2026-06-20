// CCCC_FLAGS: --build --build-dry-run --build-entry=configure
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// --build-entry=NAME selects a differently-named entry function.

[[cccc::build]]
int configure(cccc_build_ctx_t *ctx) {
    cccc_target_t *t = Executable(ctx, "conftest");
    AddSource(t, "examples/build_demo/src/main.c");
    return Build(ctx, t);
}
