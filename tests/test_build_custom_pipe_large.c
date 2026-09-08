// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: build succeeded
//
// #1322: eval_pipeline() forked and waited on one stage at a time --
// command_execute() forks *and* waitpid()s before the next stage is even
// forked. A stage writing more than one pipe buffer's worth of output
// (~16KB macOS / 64KB Linux) blocks in write() with nobody yet forked to
// read it, and the shell blocks right behind it in waitpid(): `a | b`
// deadlocked the whole build on any non-trivial payload. This generates a
// payload well past that threshold and moves it through a two-stage
// pipeline; a regression here hangs (caught by the suite's test timeout)
// rather than failing cleanly, so this only proves anything if it
// completes at all.
//
// The seed step deliberately does NOT itself use a pipe (`head -c N
// /dev/urandom`, a single command) -- an infinite producer piped into an
// early-exiting reader (`yes | head`) hits a separate, pre-existing fd-
// inheritance gap (children inherit *every* pipe fd across fork+exec, not
// just the ones dup2'd onto their own stdin/stdout, so the producer never
// sees EOF/SIGPIPE once the reader exits) that this ticket does not fix;
// filed as #1327. Piping a file that already exists, through `cat`, avoids
// that gap entirely (both stages terminate on their own).

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "seed", "head -c 300000 /dev/urandom > big.bin");
    RunCustom(ctx, "pipe", "cat big.bin | cat > copy.bin");
    RunCustom(ctx, "verify", "cmp big.bin copy.bin");
    return BuildDefault(ctx);
}
