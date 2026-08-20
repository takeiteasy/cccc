// CCCC_FLAGS: --build --build-out-dir=build/test_jobs_out --build-jobs=4
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// Verify --build-jobs=N: compiles multiple sources in parallel and still
// produces a working executable.  Uses all three sources from the build_demo
// to keep the test self-contained without adding fixtures.

[[cccc::build]]
int build_main(Builder *ctx) {
    const char  *inc = "examples/build_demo/include";

    BuildTarget *app = Executable(ctx, "jobs_app");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, inc);

    return BuildDefault(ctx);
}
