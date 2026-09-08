// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: build succeeded
//
// #1327: a pipeline whose upstream stage never terminates on its own
// (`yes | head -c N`) must not hang once the downstream reader has taken
// what it needs and exited. eval_pipeline() now sets FD_CLOEXEC on every
// pipe end it creates, so a forked stage does not inherit unrelated pipe
// read ends across exec(); once `head` exits, `yes` sees EPIPE/SIGPIPE.
//
// Payload (200000 bytes) is decisively larger than one pipe buffer on both
// platforms (~16KB macOS, ~64KB Linux). A regression makes this step hang
// rather than fail, which run_tests.py's per-subprocess timeout catches.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "producer_head", "yes | head -c 200000 > c1327_out.txt");
    RunCustom(ctx, "check_size",
              "test \"$(wc -c < c1327_out.txt)\" -eq 200000");
    return BuildDefault(ctx);
}
