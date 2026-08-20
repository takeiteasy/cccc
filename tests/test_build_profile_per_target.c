// CCCC_FLAGS: --build --build-dry-run --build-profile=debug
// CCCC_EXPECT_STDOUT: -O2
// CCCC_REJECT_STDOUT: -O0
//
// Per-target SetProfile overrides the global --build-profile.
// Global is debug (-g -O0) but the target is set to release (-O2 -DNDEBUG).
// The dry-run must show -O2 and must NOT show -O0.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
    AddSource(t, "src/main.c");
    SetProfile(t, "release"); // per-target override
    return BuildDefault(ctx);
}
