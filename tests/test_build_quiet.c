// CCCC_FLAGS: --build --build-out-dir=build/test_quiet_out --build-quiet
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// --build-quiet suppresses per-step command lines but still prints the
// final summary.  We verify the summary is present; the test runner would
// fail if the build itself errored out.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = Executable(ctx, "quiet_app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddInclude(app, "examples/build_demo/include");
    return BuildDefault(ctx);
}
