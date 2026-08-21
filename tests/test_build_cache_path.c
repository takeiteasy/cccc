// CCCC_FLAGS: --build --build-out-dir=build/test_cache_path_out --build-cache=build/test_cache_path_dir
// CCCC_EXPECT_STDOUT: build succeeded \(1 target, 0 errors\)
//
// Smoke test for --build-cache=PATH (explicit cache directory): verifies that
// an explicit cache path is accepted and the build completes successfully.

[[cccc::build]]
int build_main(Builder *ctx) {
    const char  *inc = "examples/build_demo/include";

    BuildTarget *app = Executable(ctx, "cache_path_app");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, inc);

    return BuildDefault(ctx);
}
