// CCCC_FLAGS: --testing
// glibc's fortified headers call these `__*_chk` functions directly. In bounds
// they behave like the plain function. They are glibc-only, so the cases are
// Linux-only.

#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

#ifdef __linux__
extern wchar_t *__wmemcpy_chk(wchar_t *, const wchar_t *, size_t, size_t);
extern wchar_t *__wmemmove_chk(wchar_t *, const wchar_t *, size_t, size_t);
extern wchar_t *__wmemset_chk(wchar_t *, wchar_t, size_t, size_t);
extern wchar_t *__wcscpy_chk(wchar_t *, const wchar_t *, size_t);
extern wchar_t *__wcsncpy_chk(wchar_t *, const wchar_t *, size_t, size_t);
extern wchar_t *__wcscat_chk(wchar_t *, const wchar_t *, size_t);
extern wchar_t *__wcsncat_chk(wchar_t *, const wchar_t *, size_t, size_t);
extern wchar_t *__wcpcpy_chk(wchar_t *, const wchar_t *, size_t);
extern int __swprintf_chk(wchar_t *, size_t, int, size_t, const wchar_t *, ...);
extern size_t __mbstowcs_chk(wchar_t *, const char *, size_t, size_t);
extern size_t __wcstombs_chk(char *, const wchar_t *, size_t, size_t);
extern int __wctomb_chk(char *, wchar_t, size_t);
extern char *__realpath_chk(const char *, char *, size_t);
extern int __poll_chk(struct pollfd *, unsigned long, int, size_t);
extern int __getdomainname_chk(char *, size_t, size_t);
extern long __readlinkat_chk(int, const char *, char *, size_t, size_t);
extern void __explicit_bzero_chk(void *, size_t, size_t);
extern int __asprintf_chk(char **, int, const char *, ...);
extern int __dprintf_chk(int, int, const char *, ...);
extern char *__fgets_unlocked_chk(char *, size_t, int, FILE *);
extern size_t __fread_unlocked_chk(void *, size_t, size_t, size_t, FILE *);
extern void __syslog_chk(int, int, const char *, ...);

#define UNKNOWN ((size_t)-1)

[[cccc::test]]
void test_chk_wide_copies_in_bounds(void) {
    wchar_t a[8], b[8];
    AssertEq(__wmemcpy_chk(a, L"abc", 4, 8) == a, 1);
    AssertEq(wcscmp(a, L"abc"), 0);
    AssertEq(__wmemmove_chk(b, a, 4, 8) == b, 1);
    AssertEq(__wmemset_chk(b, L'z', 2, 8) == b, 1);
    AssertEq(b[1], L'z');
    AssertEq(__wcscpy_chk(a, L"hi", 8) == a, 1);
    AssertEq(__wcscat_chk(a, L"!!", 8) == a, 1);
    AssertEq(wcscmp(a, L"hi!!"), 0);
    AssertEq(__wcsncpy_chk(b, a, 3, 8) == b, 1);
    AssertEq(__wcsncat_chk(b, L"xyz", 1, 8) == b, 1);
    AssertEq(__wcpcpy_chk(a, L"ab", 8) == a + 2, 1);
    AssertEq(__wmemcpy_chk(a, L"x", 2, UNKNOWN) == a, 1);
}

[[cccc::test]]
void test_chk_swprintf_in_bounds(void) {
    wchar_t buf[16];
    AssertEq(__swprintf_chk(buf, 16, 1, 16, L"%d-%ls", 5, L"x"), 3);
    AssertEq(wcscmp(buf, L"5-x"), 0);
}

[[cccc::test]]
void test_chk_multibyte_conversions_in_bounds(void) {
    wchar_t w[8];
    char    mb[8];
    AssertEq(__mbstowcs_chk(w, "hello", 8, 8), 5);
    AssertEq(__wcstombs_chk(mb, w, 8, 8), 5);
    AssertStrEq(mb, "hello");
    AssertEq(__wctomb_chk(mb, L'a', 8), 1);
}

[[cccc::test]]
void test_chk_stdlib_unistd_in_bounds(void) {
    char path[4096];
    AssertNotNull(__realpath_chk("/", path, sizeof path));
    AssertStrEq(path, "/");
    char dom[64];
    AssertEq(__getdomainname_chk(dom, sizeof dom, sizeof dom), 0);
    char link[64];
    AssertTrue(__readlinkat_chk(AT_FDCWD, "/proc/self/exe", link, sizeof link,
                                sizeof link) > 0);
    char z[8] = "secret";
    __explicit_bzero_chk(z, sizeof z, sizeof z);
    AssertEq(z[0], 0);
    struct pollfd p = {0, 0, 0};
    AssertTrue(__poll_chk(&p, 1, 0, sizeof p) >= 0);
}

[[cccc::test]]
void test_chk_printf_family_flag_only(void) {
    char *s = NULL;
    AssertEq(__asprintf_chk(&s, 1, "%d-%s", 4, "x"), 3);
    AssertStrEq(s, "4-x");
    free(s);
    int fds[2];
    AssertEq(pipe(fds), 0);
    AssertEq(__dprintf_chk(fds[1], 1, "a%d", 7), 2);
    close(fds[1]);
    char buf[4] = {0};
    AssertEq(read(fds[0], buf, 3), 2);
    AssertStrEq(buf, "a7");
    close(fds[0]);
    __syslog_chk(7, 1, "ignored %d", 1);
}

[[cccc::test]]
void test_chk_unlocked_readers_in_bounds(void) {
    FILE *f = tmpfile();
    AssertNotNull(f);
    fputs("abcdef\nrest\n", f);
    rewind(f);
    char line[16];
    AssertNotNull(__fgets_unlocked_chk(line, sizeof line, sizeof line, f));
    AssertStrEq(line, "abcdef\n");
    char chunk[4];
    AssertEq(__fread_unlocked_chk(chunk, sizeof chunk, 1, 4, f), 4);
    fclose(f);
}
#else
[[cccc::test]]
void test_chk_glibc_entry_points_are_linux_only(void) {
    AssertEq(1, 1);
}
#endif
