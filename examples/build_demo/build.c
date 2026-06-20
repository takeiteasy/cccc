// Build script for the demo project. Run with:
//
//     cccc --build examples/build_demo/build.c
//
// It declares three native targets — a static library, a dynamic library, and
// an executable that links against both — and builds them with the system
// toolchain. The same file is an ordinary C source: the [[cccc::build]] entry
// is only invoked in --build mode.

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
    cccc_target_add_define(app, "GREET_DEFAULT", "\"build mode\"");
    cccc_target_link_with(app, core);
    cccc_target_link_with(app, greet);

    return cccc_build_run_default(ctx);
}
