#include <fnmatch.h>

int main(void) {
    if (fnmatch("*.c", "hello.c", 0) != 0) return 1;
    if (fnmatch("*.c", "hello.h", 0) != FNM_NOMATCH) return 2;
    if (fnmatch("a?c", "abc", 0) != 0) return 3;
    if (fnmatch("*.txt", "a/b.txt", FNM_PATHNAME) != FNM_NOMATCH) return 4;
    return 42;
}
