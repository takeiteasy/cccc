// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: custom step 'andtest' failed
//
// #842: regression test for the vendored RunCustom shell's `&&` operator.
// full_command() used to check the token *after* `&&` before consuming the
// `&&` token itself, so it always saw the operator token where it expected
// SHELL_TOKEN_ATOM and rejected every `&&` command outright (parse failure,
// not a short-circuit failure). Verify `false && echo ...` fails the build.
// (The build's overall exit reflects the *last executed* command: if `&&`
// mistakenly ran both sides in sequence instead of short-circuiting, the
// trailing `echo` would succeed and the build would report success here —
// so a failing build already proves the right-hand side never ran.)

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *step = RunCustom(ctx, "andtest", "false && echo SHOULD_NOT_PRINT");
    return BuildDefault(ctx);
}
