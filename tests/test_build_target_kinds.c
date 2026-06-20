// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: build succeeded \(3 targets, 0 errors\)
// CCCC_EXPECT_STDOUT: ar rcs
// CCCC_EXPECT_STDOUT: -shared
//
// Dry-run all three target kinds (exe, static, dynamic) in one graph.
// Asserts that `ar rcs` appears for the static lib and `-shared` for the
// dynamic lib — confirming the correct toolchain verb per kind.

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    cccc_target_t *slib = cccc_static_lib(ctx, "mystat");
    cccc_target_add_source(slib, "examples/build_demo/src/lib/sum.c");

    cccc_target_t *dlib = cccc_dynamic_lib(ctx, "mydyn");
    cccc_target_add_source(dlib, "examples/build_demo/src/greet.c");

    cccc_target_t *exe = cccc_executable(ctx, "myexe");
    cccc_target_add_source(exe, "examples/build_demo/src/main.c");

    return cccc_build_run_all(ctx);
}
