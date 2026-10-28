// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: a wide string literal used as an expression is cut to a quarter of its bytes under -c=native (#1843)
// Wide-character copies, conversions, printf family and line reader that the
// fortified <wchar.h> reaches.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

[[cccc::test]]
void test_wmem_family(void) {
    wchar_t a[6], b[6] = L"hello";
    AssertEq(wmemcpy(a, b, 6) == a, 1);
    AssertEq(wcscmp(a, L"hello"), 0);
    wmemmove(a + 1, a, 4);
    AssertEq(a[1], L'h');
    wmemset(a, L'z', 5);
    AssertEq(a[4], L'z');
    AssertEq(a[5], 0);
}

[[cccc::test]]
void test_wcpcpy_wcpncpy_return_the_end(void) {
    wchar_t a[8];
    AssertEq(wcpcpy(a, L"abc") == a + 3, 1);
    AssertEq(wcscmp(a, L"abc"), 0);
    AssertEq(wcpncpy(a, L"xy", 4) == a + 2, 1);
    AssertEq(a[3], 0);
}

[[cccc::test]]
void test_wcslcpy_wcslcat(void) {
    wchar_t a[4];
    AssertEq(wcslcpy(a, L"abcdef", 4), 6);
    AssertEq(wcscmp(a, L"abc"), 0);
    wchar_t b[8] = L"ab";
    AssertEq(wcslcat(b, L"cd", 8), 4);
    AssertEq(wcscmp(b, L"abcd"), 0);
}

[[cccc::test]]
void test_swprintf_formats_wide(void) {
    wchar_t buf[32];
    AssertEq(swprintf(buf, 32, L"%d-%ls-%s", 7, L"w", "n"), 5);
    AssertEq(wcscmp(buf, L"7-w-n"), 0);
    AssertEq(swprintf(buf, 32, L"%.2f", 1.5), 4);
    AssertEq(wcscmp(buf, L"1.50"), 0);
}

static int vswprintf_wrap(wchar_t *buf, size_t n, const wchar_t *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vswprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

[[cccc::test]]
void test_vswprintf_replays_the_va_list(void) {
    wchar_t buf[32];
    AssertEq(vswprintf_wrap(buf, 32, L"%d %ls %.1f", 3, L"x", 2.5), 7);
    AssertEq(wcscmp(buf, L"3 x 2.5"), 0);
}

[[cccc::test]]
void test_mbsnrtowcs_wcsnrtombs(void) {
    const char *src = "abcdef";
    wchar_t     w[8];
    mbstate_t   st;
    memset(&st, 0, sizeof st);
    AssertEq(mbsnrtowcs(w, &src, 3, 8, &st), 3);
    AssertEq(w[2], L'c');
    const wchar_t *ws = L"xyz";
    char           c[8];
    memset(&st, 0, sizeof st);
    AssertEq(wcsnrtombs(c, &ws, 2, 8, &st), 2);
    AssertEq(c[1], 'y');
}

[[cccc::test]]
void test_fgetws_reads_a_wide_line(void) {
    // A stream keeps the orientation of its first use on glibc, so the file is
    // written on one stream and read wide on another.
    char path[] = "/tmp/cccc_fgetws_XXXXXX";
    int  fd     = mkstemp(path);
    AssertTrue(fd >= 0);
    close(fd);
    FILE *w = fopen(path, "w");
    AssertNotNull(w);
    fputs("line one\nline two\n", w);
    fclose(w);
    FILE *f = fopen(path, "r");
    AssertNotNull(f);
    wchar_t buf[32];
    AssertNotNull(fgetws(buf, 32, f));
    AssertEq(wcscmp(buf, L"line one\n"), 0);
    fclose(f);
    unlink(path);
}

#ifdef __linux__
[[cccc::test]]
void test_wmempcpy_returns_the_end(void) {
    wchar_t a[4], b[4] = L"abc";
    AssertEq(wmempcpy(a, b, 4) == a + 4, 1);
    AssertEq(wcscmp(a, L"abc"), 0);
}
#endif
