// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: multiple \[\[cccc::build\]\] entries
//
// Negative test: two [[cccc::build]] entries are ambiguous. CCCC must report
// the conflict and point at --build-entry to disambiguate.

[[cccc::build]]
int build_a(Builder *ctx) {
    return BuildAll(ctx);
}

[[cccc::build]]
int build_b(Builder *ctx) {
    return BuildAll(ctx);
}
