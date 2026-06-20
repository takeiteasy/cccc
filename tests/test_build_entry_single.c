// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// A single [[cccc::build]] entry with a non-default name (not build_main) is
// resolved automatically without --build-entry.  This exercises the
// "single attribute" precedence rule.

[[cccc::build]]
int configure(cccc_build_ctx_t *ctx) {
    cccc_target_t *t = cccc_executable(ctx, "singletest");
    cccc_target_add_source(t, "examples/build_demo/src/main.c");
    return cccc_build_run(ctx, t);
}
