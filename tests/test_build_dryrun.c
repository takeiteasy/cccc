// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: build succeeded \(3 targets, 0 errors\)
//
// Smoke test for --build mode: declares a static lib, a dynamic lib, and an
// executable linking both, then dry-runs the host runner (prints the toolchain
// command lines without spawning cc/ar/ld). Exercises topo-sort, all three
// target kinds, dependency linking and define forwarding deterministically.

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    cccc_target_t *core = cccc_static_lib(ctx, "core");
    cccc_target_add_source(core, "src/lib/sum.c");
    cccc_target_add_include(core, "include");

    cccc_target_t *greet = cccc_dynamic_lib(ctx, "greet");
    cccc_target_add_source(greet, "src/greet.c");
    cccc_target_add_include(greet, "include");

    cccc_target_t *app = cccc_executable(ctx, "app");
    cccc_target_add_source(app, "src/main.c");
    cccc_target_add_include(app, "include");
    cccc_target_add_define(app, "GREET_DEFAULT", "\"world\"");
    cccc_target_link_with(app, core);
    cccc_target_link_with(app, greet);

    return cccc_build_run_default(ctx);
}
