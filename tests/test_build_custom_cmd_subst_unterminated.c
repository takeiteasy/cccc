// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: unterminated \$\(
// CCCC_EXPECT_STDERR: custom step 'badsubst' failed
//
// #1311: a malformed `$(` must fail the build with a diagnostic naming the
// problem, not just an opaque "failed (exit N)" -- RunCustom's shell used
// to _exit() on every parse/tokenize failure with nothing printed at all.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "badsubst", "echo $(unterminated");
    return BuildDefault(ctx);
}
