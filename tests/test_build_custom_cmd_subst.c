// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: subok
// CCCC_EXPECT_STDOUT: parity-ok
//
// #1311: `$(...)` command substitution. Previously unsupported and
// silently mis-lexed -- `$(cmd)` split into two literal words at the space,
// so `[ "$(a)" = "$(b)" ]`-style parity checks in build scripts passed
// vacuously no matter what `a`/`b` printed (the ticket's own concern), and
// a substitution actually reaching command position failed with
// "shell: execvp: No such file or directory". Covers: a plain
// substitution, one nested inside another, and the parity-check shape.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "plain", "echo $(printf 'sub%s\\n' ok)");
    RunCustom(ctx, "nested", "echo $(echo $(echo nested-ok))");
    RunCustom(ctx, "parity",
              "test \"$(printf same)\" = \"$(printf same)\" && "
              "echo parity-ok");
    return BuildDefault(ctx);
}
