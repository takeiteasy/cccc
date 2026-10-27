// CCCC_FLAGS: --testing
// __builtin___*_chk: the fortified spellings glibc and Apple headers route
// calls to. A call within the object size behaves like the plain function; one
// that exceeds a known object size aborts.

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define UNKNOWN ((size_t)-1)

[[cccc::test]]
void test_chk_has_builtin(void) {
#if __has_builtin(__builtin___memcpy_chk) &&                                   \
    __has_builtin(__builtin___snprintf_chk) &&                                 \
    __has_builtin(__builtin___strcpy_chk)
    AssertEq(1, 1);
#else
    AssertEq(0, 1);
#endif
}

[[cccc::test]]
void test_chk_mem_in_bounds(void) {
    char a[8], b[8] = "abcdefg";
    AssertEq(__builtin___memcpy_chk(a, b, 8, __builtin_object_size(a, 0)) ==
                 (void *)a,
             1);
    AssertEq(memcmp(a, b, 8), 0);
    __builtin___memmove_chk(a + 1, a, 4, __builtin_object_size(a + 1, 0));
    AssertEq(a[1], 'a');
    __builtin___memset_chk(a, 'z', 8, __builtin_object_size(a, 1));
    AssertEq(a[7], 'z');
#ifndef __APPLE__
    AssertEq(__builtin___mempcpy_chk(a, b, 3, UNKNOWN) == (void *)(a + 3), 1);
#endif
}

[[cccc::test]]
void test_chk_str_in_bounds(void) {
    char a[16];
    __builtin___strcpy_chk(a, "abc", __builtin_object_size(a, 0));
    AssertEq(strcmp(a, "abc"), 0);
    __builtin___strcat_chk(a, "def", __builtin_object_size(a, 0));
    AssertEq(strcmp(a, "abcdef"), 0);
    __builtin___strncat_chk(a, "ghijk", 2, __builtin_object_size(a, 0));
    AssertEq(strcmp(a, "abcdefgh"), 0);
    char *end = __builtin___stpcpy_chk(a, "xy", UNKNOWN);
    AssertEq(end == a + 2, 1);
    __builtin___strncpy_chk(a, "hello", 16, __builtin_object_size(a, 0));
    AssertEq(strcmp(a, "hello"), 0);
    AssertEq(__builtin___stpncpy_chk(a, "ab", 4, UNKNOWN) == a + 2, 1);
}

[[cccc::test]]
void test_chk_printf_in_bounds(void) {
    char a[16];
    AssertEq(__builtin___snprintf_chk(
                 a, sizeof a, 0, __builtin_object_size(a, 0), "%d-%s", 7, "x"),
             3);
    AssertEq(strcmp(a, "7-x"), 0);
    AssertEq(
        __builtin___sprintf_chk(a, 0, __builtin_object_size(a, 0), "%05d", 42),
        5);
    AssertEq(strcmp(a, "00042"), 0);
    AssertEq(__builtin___snprintf_chk(NULL, 0, 0, 0, "%d", 12345), 5);
}

// A fortified header rewrites snprintf(...) into this builtin with every
// format argument passed through, however many there are.
[[cccc::test]]
void test_chk_snprintf_many_arguments(void) {
    char a[128];
    __builtin___snprintf_chk(a, sizeof a, 0, __builtin_object_size(a, 0),
                             "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d "
                             "%d %d %d %d",
                             1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                             16, 17, 18, 19, 20);
    AssertEq(strcmp(a, "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20"),
             0);
}

static int vfmt(char *buf, size_t n, size_t os, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = __builtin___vsnprintf_chk(buf, n, 0, os, fmt, ap);
    va_end(ap);
    return r;
}

static int vfmt_s(char *buf, size_t os, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = __builtin___vsprintf_chk(buf, 0, os, fmt, ap);
    va_end(ap);
    return r;
}

[[cccc::test]]
void test_chk_vprintf_in_bounds(void) {
    char a[16];
    AssertEq(vfmt(a, sizeof a, sizeof a, "%d", 99), 2);
    AssertEq(strcmp(a, "99"), 0);
    AssertEq(vfmt_s(a, sizeof a, "%s", "hi"), 2);
    AssertEq(strcmp(a, "hi"), 0);
}

// An unknown object size never fails.
[[cccc::test]]
void test_chk_unknown_size_never_fails(void) {
    char            a[8], b[8] = {0};
    volatile size_t n = 8;
    __builtin___memcpy_chk(a, b, n, UNKNOWN);
    AssertEq(a[0], 0);
}
