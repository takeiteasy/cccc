// CCCC_FLAGS: --build --build-entry=ghost
// CCCC_EXPECT_STDERR: entry 'ghost' not found
//
// --build-entry=NAME with a name not defined in the build script must error.

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    return BuildAll(ctx);
}
