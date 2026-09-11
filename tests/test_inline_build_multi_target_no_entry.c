// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: (?=.*mt_a)(?=.*mt_b)
//
// #1272: two @build_target factories, no [[cccc::build]] entry and no
// build_main -- the entry-less fallback runs every factory (equivalent to
// BuildAll), not just the first, so both targets appear in the dry-run plan.

[[cccc::build_target]]
BuildTarget *mt_a(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "mt_a");
    AddSourceStr(t, "mt_a.c", "int main(void) { return 0; }\n");
    return t;
}

[[cccc::build_target]]
BuildTarget *mt_b(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "mt_b");
    AddSourceStr(t, "mt_b.c", "int main(void) { return 0; }\n");
    return t;
}
