// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: build succeeded
//
// #1325: a shell builtin (cd/pwd/exit) honours the stage's redirect and pipe
// fds and reports a real exit status, instead of always running in the parent
// (writing to the shell's own stdout) and always "succeeding".
//   - `pwd > f`      : f gets the working directory, not the shell's stdout.
//   - `pwd | wc -c`  : the builtin's stdout reaches the pipe (subshell).
//   - `cd <bad-dir>` : a failing builtin exits non-zero, so `||` fires.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "pwd_redir", "pwd > c1325_p.txt");
    RunCustom(
        ctx, "check_p",
        "test -s c1325_p.txt && test \"$(cat c1325_p.txt)\" = \"$(pwd)\"");

    RunCustom(ctx, "pwd_pipe", "pwd | wc -c > c1325_n.txt");
    RunCustom(ctx, "check_n", "test \"$(cat c1325_n.txt)\" -gt 1");

    RunCustom(ctx, "cd_status",
              "cd /cccc/no/such/dir || echo caught > c1325_cd.txt");
    RunCustom(ctx, "check_cd", "test \"$(cat c1325_cd.txt)\" = caught");

    return BuildDefault(ctx);
}
