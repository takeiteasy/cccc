// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: build succeeded
//
// #1322: eval_redirection() hardcoded ctx->input_fd/output_fd to -1 on exit
// instead of restoring whatever eval_pipeline() had put there, so a
// redirect-carrying stage in the *middle* of a 3+ stage pipeline
// (`a | b > f | c`) wiped out the previous stage's pipe read end in ctx.
// eval_pipeline()'s own cleanup (`close(ctx->input_fd)`) then closed -1
// instead of that real fd, leaking it.
//
// Made deterministic without relying on a specific fd-table implementation:
// each RunCustom step runs in its own forked shell child (posix_shell_with_
// io forks once per command string), so a leak is confined to one command
// string. Count open fds before and after the same 3-stage pipeline
// (`cat in | cat > mid | cat > /dev/null`) within a single RunCustom
// invocation -- pre-fix the counts differ by one (the leaked pipe read
// end); post-fix they match. `ls /dev/fd` (BSD/macOS) and its Linux
// equivalent (`/dev/fd` -> `/proc/self/fd`, "self" resolving to whichever
// process reads it) both work the same way here: `ls` is forked from the
// leaking shell process, so it inherits that leaked fd via plain fork()
// and its own listing reflects it, on both platforms -- confirmed the
// fork-inheritance half of that directly in a Linux (aarch64) container;
// the actual pre-fix/post-fix count divergence itself was verified on
// macOS, this ticket's development platform.
//
// The second target separately pins the POSIX redirect-beats-pipe data
// routing rule (verified against real /bin/sh and bash): `cmd > f | wc`
// puts cmd's output in `f`, and the downstream pipe stage sees an empty
// input. This was already correct before this ticket -- #1322's filed
// symptom ("the pipe never receives the stage's output") described this as
// a bug, but it isn't; the real defect was the fd leak above. Kept here so
// a future change can't silently flip this to the wrong behavior.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "seed", "printf 'hello\\n' > in.txt");
    RunCustom(ctx, "leak_check",
              "ls /dev/fd | wc -l > n1.txt ; "
              "cat in.txt | cat > mid.txt | cat > /dev/null ; "
              "ls /dev/fd | wc -l > n2.txt ; "
              "cmp n1.txt n2.txt");
    RunCustom(ctx, "routing_check",
              "cat in.txt > routed.txt | wc -c > pipe_side.txt ; "
              "test \"$(cat routed.txt)\" = hello ; "
              "grep -q '^[[:space:]]*0$' pipe_side.txt");
    RunCustom(ctx, "cleanup",
              "rm -f in.txt n1.txt mid.txt n2.txt routed.txt pipe_side.txt");
    return BuildDefault(ctx);
}
