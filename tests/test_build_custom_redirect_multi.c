// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: world
//
// #1311 follow-up: redirection order must not matter (`cmd > out < in` is
// just as valid as `cmd < in > out`) -- command()'s fix loops over any
// number/order of trailing '<'/'>' tokens rather than checking each once.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "seed", "printf 'world\\n' > cccc_test_1311m_in.txt");
    RunCustom(ctx, "redirect",
              "cat > cccc_test_1311m_out.txt < cccc_test_1311m_in.txt");
    RunCustom(ctx, "verify", "cat cccc_test_1311m_out.txt");
    RunCustom(ctx, "cleanup",
              "rm -f cccc_test_1311m_in.txt cccc_test_1311m_out.txt");
    return BuildDefault(ctx);
}
