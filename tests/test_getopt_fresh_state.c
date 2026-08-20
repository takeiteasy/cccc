// Regression test for #1041: cccc's own CLI argument parsing (main.c,
// via the host's real getopt_long()) shares process-global getopt() state
// with a guest program's own getopt() calls -- optind/opterr are host
// process globals, not a guest-private copy (include/getopt.h's macros
// resolve to accessor shims returning pointers straight into that state,
// #736/#1040). Confirmed with the ticket's own repro: `./cccc -I./include
// dbg.c` (the harness's normal invocation shape, tools/testing/runner.py
// always passes -I./include) left optind/opterr wherever cccc's own
// argument parsing had left them, not fresh.
//
// Deliberately does NOT reset optind/opterr itself (unlike
// test_getopt_state_shims.c, which legitimately re-parses a second,
// unrelated argv mid-process and needs its own reset for that) -- the
// whole point of this test is to prove cc_run() does that reset
// (cccc_reset_getopt_state(), src/vm.c) before the guest's very first
// getopt() call, matching how a real freshly-exec'd process would start.

#include <getopt.h>
#include <string.h>

int main(void) {
    char *argv[] = {"prog", "-x", "val", "-y"};
    int   argc   = 4;

    int   c      = getopt(argc, argv, "x:y");
    if (c != 'x')
        return 1;
    if (!optarg || strcmp(optarg, "val") != 0)
        return 2;
    if (optind != 3)
        return 3;

    // cccc's own CLI parsing sets opterr=0 to handle its own diagnostics
    // (main.c) -- a guest program never asked for that and expects the
    // libc default (1), or its own getopt() error messages go missing.
    if (opterr != 1)
        return 4;

    c = getopt(argc, argv, "x:y");
    if (c != 'y')
        return 5;
    if (optind != 4)
        return 6;

    return 42;
}
