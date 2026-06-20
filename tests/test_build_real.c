// CCCC_FLAGS: --build --build-out-dir=build/test_out
// CCCC_EXPECT_STDOUT: build succeeded \(3 targets, 0 errors\)
//
// Real end-to-end build: invokes cc/ar/ld to produce actual artifacts under
// build/test_out/.  Exercises all three target kinds with transitive deps.

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    const char *inc = "examples/build_demo/include";

    cccc_target_t *core = cccc_static_lib(ctx, "core");
    cccc_target_add_source(core, "examples/build_demo/src/lib/sum.c");
    cccc_target_add_include(core, inc);

    cccc_target_t *greet = cccc_dynamic_lib(ctx, "greet");
    cccc_target_add_source(greet, "examples/build_demo/src/greet.c");
    cccc_target_add_include(greet, inc);

    cccc_target_t *app = cccc_executable(ctx, "app");
    cccc_target_add_source(app, "examples/build_demo/src/main.c");
    cccc_target_add_include(app, inc);
    cccc_target_link_with(app, core);
    cccc_target_link_with(app, greet);

    return cccc_build_run_default(ctx);
}
