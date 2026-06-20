// CCCC_FLAGS: --build --build-dry-run --build-entry=configure
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// --build-entry=NAME selects a differently-named entry function.

[[cccc::build]]
int configure(cccc_build_ctx_t *ctx) {
    cccc_target_t *t = cccc_executable(ctx, "conftest");
    cccc_target_add_source(t, "examples/build_demo/src/main.c");
    return cccc_build_run(ctx, t);
}
