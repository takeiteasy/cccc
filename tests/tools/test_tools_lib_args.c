// tools/lib/args: flags, valued flags (all spellings), positionals.
#include "../../tools/lib/args.c"
#include <string.h>

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #c);                                                       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int parse(int argc, char **argv, int *check, const char **bin,
                 const char **jobs, ArgResult *r) {
    *check          = 0;
    *bin            = NULL;
    *jobs           = NULL;
    ArgSpec specs[] = {
        {"--check", NULL, 0, check, NULL},
        {"--binary", NULL, 1, NULL, bin},
        {"--jobs", "-j", 1, NULL, jobs},
    };
    *r = args_parse(argc, argv, specs, 3, "tool [--check] [--binary P] [-j N]");
    return 0;
}

int main(void) {
    int         check;
    const char *bin, *jobs;
    ArgResult   r;

    char       *a1[] = {"t"};
    parse(1, a1, &check, &bin, &jobs, &r);
    CHECK(!check && !bin && !jobs && r.npos == 0);

    char *a2[] = {"t", "--check", "--binary", "build/cccc", "-j", "8", "x.c"};
    parse(7, a2, &check, &bin, &jobs, &r);
    CHECK(check && strcmp(bin, "build/cccc") == 0 && strcmp(jobs, "8") == 0);
    CHECK(r.npos == 1 && strcmp(r.pos[0], "x.c") == 0);

    char *a3[] = {"t", "--binary=./cccc", "-j4", "p", "q"};
    parse(5, a3, &check, &bin, &jobs, &r);
    CHECK(!check && strcmp(bin, "./cccc") == 0 && strcmp(jobs, "4") == 0);
    CHECK(r.npos == 2 && strcmp(r.pos[1], "q") == 0);

    char *a4[] = {"t", "--jobs=2", "-"};
    parse(3, a4, &check, &bin, &jobs, &r);
    CHECK(strcmp(jobs, "2") == 0 && r.npos == 1 && strcmp(r.pos[0], "-") == 0);

    // the error paths exit(1) and are covered by the proc test's subprocess
    return 42;
}
