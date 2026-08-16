// Ticket #1040: optarg/optind/opterr/optopt accessor shims must not
// collide with include/getopt.h's own extern declarations under
// -c=native/-c=generated -- same bug class as test_stdio_stream_shims.c.

#include <getopt.h>
#include <string.h>

int main(void) {
    // optind is real host process state (shared with cccc's own CLI
    // argument parsing, which itself calls the host's getopt_long() --
    // src/main.c:1225) -- reset it before parsing this test's own synthetic
    // argv, the same way any program re-parsing argv mid-process must.
    optind = 1;
    char *argv[] = {"prog", "-x", "val", "-y"};
    int argc = 4;

    int c = getopt(argc, argv, "x:y");
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
