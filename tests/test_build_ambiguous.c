// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: multiple \[\[cccc::build\]\] entries
//
// Negative test: two [[cccc::build]] entries are ambiguous. CCCC must report
// the conflict and point at --build-entry to disambiguate.

[[cccc::build]]
int build_a(cccc_build_ctx_t *ctx) {
    return BuildAll(ctx);
}

[[cccc::build]]
int build_b(cccc_build_ctx_t *ctx) {
    return BuildAll(ctx);
}
