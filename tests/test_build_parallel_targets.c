// CCCC_FLAGS: --build --build-out-dir=build/test_par_out --build-jobs=2
// CCCC_EXPECT_STDOUT: build succeeded \(3 targets, 0 errors\)
//
// Verify target-level DAG parallelism (#557): two independent static libs
// (par_sum and par_greet) build simultaneously, then the executable that
// links both.  Uses build_demo sources to stay self-contained.

[[cccc::build]]
int build_main(Builder *ctx) {
    const char  *inc     = "examples/build_demo/include";

    BuildTarget *par_sum = StaticLib(ctx, "par_sum");
    AddSource(par_sum, "examples/build_demo/src/lib/sum.c");
    AddInclude(par_sum, inc);

    BuildTarget *par_greet = StaticLib(ctx, "par_greet");
    AddSource(par_greet, "examples/build_demo/src/greet.c");
    AddInclude(par_greet, inc);

    BuildTarget *app = Executable(ctx, "par_app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, inc);
    LinkWith(app, par_sum);
    LinkWith(app, par_greet);

    return BuildDefault(ctx);
}
