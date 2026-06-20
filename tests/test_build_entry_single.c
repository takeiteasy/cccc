// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// A single [[cccc::build]] entry with a non-default name (not build_main) is
// resolved automatically without --build-entry.  This exercises the
// "single attribute" precedence rule.

[[cccc::build]]
int configure(void) {
    cccc_target_t *t = Executable("singletest");
    AddSource(t, "examples/build_demo/src/main.c");
    return Build(t);
}
