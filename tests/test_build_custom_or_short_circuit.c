// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: build succeeded
//
// #842: regression test for the vendored RunCustom shell's `||` operator.
// `false || true` must parse (previously `||` lexed as two SHELL_TOKEN_PIPE
// tokens and there was no SHELL_AST_OR at all) and the right-hand side must
// run because the left-hand side failed.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *step = RunCustom(ctx, "ortest", "false || true");
    return BuildDefault(ctx);
}
