// CCCC_EXPECT_LEAK: Apple's libc _strfmon leaks its scratch buffer
// strfmon(buf, sizeof buf, "%n", 1234.56) succeeds and contains "1234"; a
// multi-conversion format with literal text between two directives; a maxsize
// too small for the output returns -1/E2BIG.
//
// Under ASan on macOS, calling strfmon() at all trips a heap-buffer-overflow
// inside Apple's own libc _strfmon, a host-libc over-read that glibc does not
// share. It is suppressed for ASan builds via the __asan_default_suppressions
// hook in src/stdlib/posix.c.

#include <monetary.h>
#include <string.h>
#include <sys/types.h>

int main(void) {
    char    buf[64] = {0};
    ssize_t n       = strfmon(buf, sizeof(buf), "%n", 1234.56);
    if (n < 0)
        return 1;
    if (!strstr(buf, "1234"))
        return 2;

    char    buf2[64] = {0};
    ssize_t n2 = strfmon(buf2, sizeof(buf2), "Price: %n and %n", 10.5, 20.5);
    if (n2 < 0)
        return 3;
    if (!strstr(buf2, "Price:") || !strstr(buf2, "10.5") ||
        !strstr(buf2, "20.5"))
        return 4;

    char small[4];
    if (strfmon(small, sizeof(small), "%n", 1234567.89) != -1)
        return 5;

    return 42;
}
