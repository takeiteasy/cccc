// CCCC_FLAGS: --build --build-out-dir=build/test_out
// CCCC_EXPECT_STDOUT: build succeeded \(3 targets, 0 errors\)
//
// Real end-to-end build: invokes cc/ar/ld to produce actual artifacts under
// build/test_out/.  Exercises all three target kinds with transitive deps.

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *inc = "examples/build_demo/include";

    BuildTarget *core = StaticLib(ctx, "core");
    AddSource(core, "examples/build_demo/src/lib/sum.c");
    AddInclude(core, inc);

    BuildTarget *greet = DynamicLib(ctx, "greet");
    AddSource(greet, "examples/build_demo/src/greet.c");
    AddInclude(greet, inc);

    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, inc);
    LinkWith(app, core);
    LinkWith(app, greet);

    return BuildDefault(ctx);
}
