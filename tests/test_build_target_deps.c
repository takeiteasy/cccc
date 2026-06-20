// CCCC_FLAGS: --build --build-dry-run --build-target=app
// CCCC_EXPECT_STDOUT: ar rcs
// CCCC_EXPECT_STDOUT: -shared
// CCCC_EXPECT_STDOUT: build succeeded \(3 targets, 0 errors\)
//
// --build-target=app keeps transitive deps (core static lib + greet dynamic
// lib) because app links_with both.  All three targets must be built.

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    cccc_target_t *core = cccc_static_lib(ctx, "core");
    cccc_target_add_source(core, "examples/build_demo/src/lib/sum.c");

    cccc_target_t *greet = cccc_dynamic_lib(ctx, "greet");
    cccc_target_add_source(greet, "examples/build_demo/src/greet.c");

    cccc_target_t *app = cccc_executable(ctx, "app");
    cccc_target_add_source(app, "examples/build_demo/src/main.c");
    cccc_target_link_with(app, core);
    cccc_target_link_with(app, greet);

    return cccc_build_run_default(ctx);
}
