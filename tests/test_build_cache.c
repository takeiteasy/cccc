// CCCC_FLAGS: --build --build-out-dir=build/test_cache_out --build-cache
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// Smoke test for --build-cache (no explicit path): the runner should enable
// incremental caching with the default cache dir (<out-dir>/.cccc-cache) and
// still produce a working build.  Uses the same demo sources as test_build_jobs.

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *inc = "examples/build_demo/include";

    BuildTarget *app = Executable(ctx, "cache_app");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, inc);

    return BuildDefault(ctx);
}
