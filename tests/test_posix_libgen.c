#include <libgen.h>
#include <string.h>

int main(void) {
    char p1[] = "/usr/lib";
    char p2[] = "/usr/";
    char p3[] = "usr";

    if (strcmp(basename(p1), "lib") != 0) return 1;
    if (strcmp(basename(p2), "usr") != 0) return 2;
    if (strcmp(basename(p3), "usr") != 0) return 3;

    if (strcmp(dirname(p1), "/usr") != 0) return 4;
    if (strcmp(dirname(p2), "/") != 0) return 5;
    if (strcmp(dirname(p3), ".") != 0) return 6;

    return 42;
}
