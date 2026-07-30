// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: custom step 'badcmd' failed
//
// #842: a trailing `&&` with no right-hand command is malformed and must
// fail the build (not silently succeed). Guards the shell_eval_parser NULL
// path: a parse failure must propagate as a non-zero exit, never as a
// quietly-successful no-op.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *step = RunCustom(ctx, "badcmd", "echo a &&");
    return BuildDefault(ctx);
}
