// CCCC_FLAGS: --build --build-dry-run --build-target=core
// CCCC_EXPECT_STDOUT: ar rcs
// CCCC_REJECT_STDOUT: myapp
//
// --build-target=core prunes to that single target; the exe and dynamic lib
// must not appear in the dry-run output.

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    cccc_target_t *core = StaticLib(ctx, "core");
    AddSource(core, "examples/build_demo/src/lib/sum.c");

    cccc_target_t *greet = DynamicLib(ctx, "greet");
    AddSource(greet, "examples/build_demo/src/greet.c");

    cccc_target_t *app = Executable(ctx, "myapp");
    AddSource(app, "examples/build_demo/src/main.c");
    LinkWith(app, core);
    LinkWith(app, greet);

    return BuildDefault(ctx);
}
