#include <glob.h>
#include <string.h>

int main(void) {
    glob_t g;
    int rc = glob("tests/test_posix_glob.c", 0, 0, &g);
    if (rc != 0) return 1;
    if (g.gl_pathc != 1) return 2;
    if (!g.gl_pathv) return 3;
    if (strcmp(g.gl_pathv[0], "tests/test_posix_glob.c") != 0) return 4;
    globfree(&g);

    rc = glob("tests/no-such-posix-glob-file-*.c", 0, 0, &g);
    if (rc != GLOB_NOMATCH) return 5;
    return 42;
}
