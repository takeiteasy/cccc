// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: must not define main
//
// Negative test: a --build script may not define main(). CCCC must reject this
// before running anything.

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    return BuildAll(ctx);
}

int main(void) {
    return 0;
}
