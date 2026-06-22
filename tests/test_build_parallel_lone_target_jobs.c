// CCCC_FLAGS: --build --build-out-dir=build/test_lonepar_out --build-jobs=4
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// Regression guard for target-level parallelism (#557): a single target with
// multiple sources and --build-jobs=4 must still use source-level parallelism
// (runs in-process, not forked with jobs=1).  Correctness only; timing is not
// asserted.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = Executable(ctx, "lone_app");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, "examples/build_demo/include");
    return BuildDefault(ctx);
}
