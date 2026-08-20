// Ticket #1040: optarg/optind/opterr/optopt accessor shims must not
// collide with include/getopt.h's own extern declarations under
// -c=native/-c=generated -- same bug class as test_stdio_stream_shims.c.

#include <getopt.h>
#include <string.h>

int main(void) {
    // optind is real host process state. cc_run() resets it fresh before
    // every guest run now (#1041; see test_getopt_fresh_state.c for the
    // regression test that exercises that reset with no explicit reset of
    // its own), but this test's own synthetic argv below is unrelated to
    // whatever cccc's own CLI parsing left behind -- reset explicitly
    // before parsing it, the same way any program re-parsing a *second*,
    // different argv mid-process must.
    optind       = 1;
    char *argv[] = {"prog", "-x", "val", "-y"};
    int   argc   = 4;

    int   c      = getopt(argc, argv, "x:y");
    if (c != 'x')
        return 1;
    if (!optarg || strcmp(optarg, "val") != 0)
        return 2;

    c = getopt(argc, argv, "x:y");
    if (c != 'y')
        return 3;

    if (optind != 4)
        return 4;

    return 42;
}
