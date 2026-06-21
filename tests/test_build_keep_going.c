// CCCC_FLAGS: --build --build-out-dir=build/test_kg_out --build-keep-going
// CCCC_EXPECT_STDERR: target 'mustfail' failed, continuing
// CCCC_EXPECT_STDOUT: build failed \(1 error\)
//
// --build-keep-going: a failed target does not stop independent targets from
// building.  We declare a custom step that always fails, then an executable
// with no dependency on it; with keep-going both are attempted and the final
// summary lists the one failure.

[[cccc::build]]
int build_main(Builder *ctx) {
    // This step will fail; without --build-keep-going the build stops here.
    BuildTarget *bad = RunCustom(ctx, "mustfail", "false");
    (void)bad;

    // This target is independent and should still be attempted.
    BuildTarget *app = Executable(ctx, "kg_app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddInclude(app, "examples/build_demo/include");

    return BuildDefault(ctx);
}
