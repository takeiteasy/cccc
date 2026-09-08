// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: three
//
// #1322: baseline pipeline coverage. There was no RunCustom test exercising
// eval_pipeline() at all before this ticket, which is how both the fd-leak
// and the fork/wait deadlock (see test_build_custom_pipe_large.c) went
// unnoticed. A plain three-stage pipeline must still route data correctly
// end to end.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "pipe", "printf 'one\\ntwo\\nthree\\n' | tail -n 1 | cat");
    return BuildDefault(ctx);
}
