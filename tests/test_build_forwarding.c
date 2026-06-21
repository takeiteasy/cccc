// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: -UOLD_DEFINE
// CCCC_EXPECT_STDOUT: -ffast-math
// CCCC_EXPECT_STDOUT: -Wl,-rpath,/opt/lib
//
// Dry-run test for flag forwarding: add_undef, add_cflag, add_ldflag.
// Each flag must appear verbatim in the printed command lines.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "flagtest");
    AddSource(t, "examples/build_demo/src/main.c");
    AddUndef(t, "OLD_DEFINE");
    AddCFlag(t, "-ffast-math");
    AddLdFlag(t, "-Wl,-rpath,/opt/lib");
    return Build(ctx, t);
}
