// CCCC_FLAGS: --build --build-dry-run --build-target=core
// CCCC_EXPECT_STDOUT: ar rcs
// CCCC_REJECT_STDOUT: myapp
//
// --build-target=core prunes to that single target; the exe and dynamic lib
// must not appear in the dry-run output.

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    cccc_target_t *core = cccc_static_lib(ctx, "core");
    cccc_target_add_source(core, "examples/build_demo/src/lib/sum.c");

    cccc_target_t *greet = cccc_dynamic_lib(ctx, "greet");
    cccc_target_add_source(greet, "examples/build_demo/src/greet.c");

    cccc_target_t *app = cccc_executable(ctx, "myapp");
    cccc_target_add_source(app, "examples/build_demo/src/main.c");
    cccc_target_link_with(app, core);
    cccc_target_link_with(app, greet);

    return cccc_build_run_default(ctx);
}
