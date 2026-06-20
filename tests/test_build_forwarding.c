// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: -UOLD_DEFINE
// CCCC_EXPECT_STDOUT: -ffast-math
// CCCC_EXPECT_STDOUT: -Wl,-rpath,/opt/lib
//
// Dry-run test for flag forwarding: add_undef, add_cflag, add_ldflag.
// Each flag must appear verbatim in the printed command lines.

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    cccc_target_t *t = cccc_executable(ctx, "flagtest");
    cccc_target_add_source(t, "examples/build_demo/src/main.c");
    cccc_target_add_undef(t, "OLD_DEFINE");
    cccc_target_add_cflag(t, "-ffast-math");
    cccc_target_add_ldflag(t, "-Wl,-rpath,/opt/lib");
    return cccc_build_run(ctx, t);
}
