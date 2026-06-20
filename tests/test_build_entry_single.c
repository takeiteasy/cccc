// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// A single [[cccc::build]] entry with a non-default name (not build_main) is
// resolved automatically without --build-entry.  This exercises the
// "single attribute" precedence rule.

[[cccc::build]]
int configure(cccc_build_ctx_t *ctx) {
    cccc_target_t *t = Executable(ctx, "singletest");
    AddSource(t, "examples/build_demo/src/main.c");
    return Build(ctx, t);
}
