// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: YES
//
// #842: `true && echo YES` must parse and run the right-hand side once the
// left-hand side succeeds. Companion to test_build_custom_and_short_circuit.c.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *step = RunCustom(ctx, "andtest", "true && echo YES");
    return BuildDefault(ctx);
}
