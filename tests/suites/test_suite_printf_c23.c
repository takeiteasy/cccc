// CCCC_FLAGS: --testing
// Consolidated suite: C23 binary format specifiers (%b/%B)
// Source tests: test_printf_binary, test_scanf_binary, test_vprintf_binary
//   (moved here from test_suite_misc.c, #1120)
//
// Skipped by the --native corpus on every platform (NATIVE_SKIP_TESTS in
// tools/testing/__init__.py). CCCC's VM formats through its own C23
// formatter (src/stdlib/format_printf.c/format_scanf.c), so these pass
// there; -c=native output calls real host libc. macOS 15 libc implements
// neither the C23 %b nor %B conversion in printf or scanf -- printf emits
// a literal 'b' and sscanf reports zero matches. glibc carries printf %b
// since 2.35 and scanf %b since 2.38, but glibc's scanf has no %B
// conversion specifier at all (printf's b/B are interchangeable there
// since the case only selects the 0b/0B prefix spelling on output, a
// distinction that doesn't exist for scanf input), so Linux fails only
// the one %B-via-scanf case rather than the whole file the way macOS
// does. Permanent platform gap, same disposition as reallocarray (#1028)
// and the fmaximum family (#1037) -- see NATIVE.md.

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// [from test_vprintf_binary]
static int misc_vb_my_sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}
static int misc_vb_my_sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

// [from test_printf_binary]
// C23 %b/%B binary integer format specifier.
[[cccc::test(return = 42)]]
int test_printf_binary(void) {
    char buf[64];
    sprintf(buf, "%b", 42u);
    if (strcmp(buf, "101010") != 0)
        return 1;
    sprintf(buf, "%#b", 42u);
    if (strcmp(buf, "0b101010") != 0)
        return 2;
    sprintf(buf, "%B", 10u);
    if (strcmp(buf, "1010") != 0)
        return 3;
    int v = 0;
    sscanf("101010", "%b", &v);
    if (v != 42)
        return 4;
    return 42;
}

// [from test_scanf_binary]
// C23 %b/%B binary conversion specifier.
[[cccc::test(return = 42)]]
int test_scanf_binary(void) {
    int a = 0, b = 0, c = 0, d = 0;
    sscanf("101010", "%b", &a);
    if (a != 42)
        return 1;
    sscanf("0b101010", "%b", &b);
    if (b != 42)
        return 2;
    sscanf("0B1111", "%B", &c);
    if (c != 15)
        return 3;
    sscanf("101111", "%4b", &d);
    if (d != 11)
        return 4; // 0b1011 = 11
    int x = 0, y = 0, z = 0;
    int n = sscanf("10 0x1F 0b110", "%d %x %b", &x, &y, &z);
    if (n != 3 || x != 10 || y != 31 || z != 6)
        return 5;
    return 42;
}

// [from test_vprintf_binary]
// %b/%B with v*printf/v*scanf multi-arg forwarding.
[[cccc::test(return = 42)]]
int test_vprintf_binary(void) {
    char buf[64];
    misc_vb_my_sprintf(buf, "%#b", 255u);
    if (strcmp(buf, "0b11111111") != 0)
        return 1;
    int v = 0;
    misc_vb_my_sscanf("0b101", "%b", &v);
    if (v != 5)
        return 2;
    misc_vb_my_sprintf(buf, "%d+%d=%d", 10, 20, 30);
    if (strcmp(buf, "10+20=30") != 0)
        return 3;
    int a = 0, b = 0, c = 0;
    misc_vb_my_sscanf("7 8 9", "%d %d %d", &a, &b, &c);
    if (a != 7 || b != 8 || c != 9)
        return 4;
    return 42;
}
