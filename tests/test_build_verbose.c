// CCCC_FLAGS: --build --build-out-dir=build/test_verbose_out --build-verbose
// CCCC_EXPECT_STDOUT: >> target 'verbose_app' \[executable
//
// --build-verbose emits a per-target header before each target is compiled.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = Executable(ctx, "verbose_app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddInclude(app, "examples/build_demo/include");
    return BuildDefault(ctx);
}
