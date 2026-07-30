// CCCC_FLAGS: --build --build-out-dir=build/test_glob_deferred_out
// CCCC_EXPECT_STDOUT: gen_extra\.c
// CCCC_EXPECT_STDOUT: build succeeded
//
// #851: AddSourcesGlobDeferred expands at build_target() time, after this
// target's RunCustom codegen dependency has already run -- so it can pick up
// a file the dependency creates during this same build. AddSourcesGlob (the
// immediate variant) expands at declaration time and cannot see such a file.
// Verified end-to-end (not dry-run, since the codegen step must actually
// execute): the compile line for the generated source must appear.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *gen = RunCustom(ctx, "gen-extra",
        "rm -rf build/test_glob_deferred_gen && mkdir -p build/test_glob_deferred_gen && "
        "printf 'int extra_val(void) { return 7; }\\n' > build/test_glob_deferred_gen/gen_extra.c");

    BuildTarget *app = Executable(ctx, "glob_deferred_app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddInclude(app, "examples/build_demo/include");
    AddSourcesGlobDeferred(app, "build/test_glob_deferred_gen/*.c");
    DependsOn(app, gen);

    return BuildDefault(ctx);
}
