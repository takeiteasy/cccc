// #1042 side discovery: strspn/strcspn/strcoll are registered VM cfuncs
// (src/stdlib/string.c) but were never declared in include/string.h, so
// under -c=native's -I./include forwarding the real host cc saw only an
// implicit declaration (strspn/strcspn: a hard C99+ error) or nothing at
// all (strcoll: "use of undeclared identifier"). Found auditing
// tests/test_minilua.c, which calls all three. Fixed by adding plain
// extern declarations to include/string.h, matching the file's existing
// `long` return-type convention for a size_t-shaped value (see strxfrm).

#include <string.h>

int main(void) {
    if (strspn("abcdef123", "abcdef") != 6)
        return 1;
    if (strspn("abcdef123", "xyz") != 0)
        return 2;

    if (strcspn("abc123def", "0123456789") != 3)
        return 3;
    if (strcspn("abcdef", "xyz") != 6) // no reject chars found -> whole string
        return 4;

    if (strcoll("abc", "abc") != 0)
        return 5;
    if (strcoll("abc", "abd") >= 0)
        return 6;
    if (strcoll("abd", "abc") <= 0)
        return 7;

    return 42;
}
