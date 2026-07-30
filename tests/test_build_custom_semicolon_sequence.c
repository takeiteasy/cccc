// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: a.*b
//
// #842: regression test for the vendored RunCustom shell's `;` operator.
// full_command() checked the token after `;` before consuming the `;` token
// itself (the same bug class as `&&`), so `echo a; echo b` always failed to
// parse. Verify both commands run, in order.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *step = RunCustom(ctx, "semitest", "echo a; echo b");
    return BuildDefault(ctx);
}
