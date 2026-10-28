// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: the host's fortify failure signal differs (SIGTRAP on Darwin)
// A __builtin___*_chk call that exceeds a known object size aborts like libc's
// __chk_fail. The volatile count keeps a host compiler from folding the call.

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

[[cccc::test(exit_code = 134)]]
void test_chk_memcpy_overflow_aborts(void) {
    char            a[8], b[16] = {0};
    volatile size_t n = 16;
    __builtin___memcpy_chk(a, b, n, __builtin_object_size(a, 0));
}

[[cccc::test(exit_code = 134)]]
void test_chk_memset_overflow_aborts(void) {
    char            a[8];
    volatile size_t n = 16;
    __builtin___memset_chk(a, 0, n, __builtin_object_size(a, 0));
}

[[cccc::test(exit_code = 134)]]
void test_chk_strcpy_overflow_aborts(void) {
    char                 a[4];
    volatile const char *s = "too long for four";
    __builtin___strcpy_chk(a, (const char *)s, __builtin_object_size(a, 0));
}

[[cccc::test(exit_code = 134)]]
void test_chk_snprintf_overflow_aborts(void) {
    char            a[8];
    volatile size_t n = 16;
    __builtin___snprintf_chk(a, n, 0, __builtin_object_size(a, 0), "%d", 1);
}

// glibc's own entry points, called as its fortified headers do.
#ifdef __linux__
extern wchar_t *__wmemcpy_chk(wchar_t *, const wchar_t *, size_t, size_t);
extern wchar_t *__wcscpy_chk(wchar_t *, const wchar_t *, size_t);
extern wchar_t *__wcsncpy_chk(wchar_t *, const wchar_t *, size_t, size_t);
extern int __swprintf_chk(wchar_t *, size_t, int, size_t, const wchar_t *, ...);
extern size_t __mbstowcs_chk(wchar_t *, const char *, size_t, size_t);
extern size_t __wcstombs_chk(char *, const wchar_t *, size_t, size_t);
extern int __wctomb_chk(char *, wchar_t, size_t);
extern char *__realpath_chk(const char *, char *, size_t);
extern int __poll_chk(struct pollfd *, unsigned long, int, size_t);
extern int __getdomainname_chk(char *, size_t, size_t);
extern long __readlinkat_chk(int, const char *, char *, size_t, size_t);
extern void __explicit_bzero_chk(void *, size_t, size_t);
extern char *__getwd_chk(char *, size_t);
extern char *__fgets_unlocked_chk(char *, size_t, int, FILE *);

[[cccc::test(exit_code = 134)]]
void test_chk_wmemcpy_overflow_aborts(void) {
    wchar_t         a[4], b[8] = {0};
    volatile size_t n = 8;
    __wmemcpy_chk(a, b, n, 4);
}

[[cccc::test(exit_code = 134)]]
void test_chk_wcscpy_overflow_aborts(void) {
    wchar_t a[4];
    __wcscpy_chk(a, L"too long", 4);
}

[[cccc::test(exit_code = 134)]]
void test_chk_wcsncpy_overflow_aborts(void) {
    wchar_t         a[4];
    volatile size_t n = 8;
    __wcsncpy_chk(a, L"abc", n, 4);
}

[[cccc::test(exit_code = 134)]]
void test_chk_swprintf_overflow_aborts(void) {
    wchar_t         a[4];
    volatile size_t n = 16;
    __swprintf_chk(a, n, 1, 4, L"%d", 1);
}

[[cccc::test(exit_code = 134)]]
void test_chk_mbstowcs_overflow_aborts(void) {
    wchar_t         a[4];
    volatile size_t n = 8;
    __mbstowcs_chk(a, "abc", n, 4);
}

[[cccc::test(exit_code = 134)]]
void test_chk_wcstombs_overflow_aborts(void) {
    char            a[4];
    volatile size_t n = 8;
    __wcstombs_chk(a, L"abc", n, 4);
}

[[cccc::test(exit_code = 134)]]
void test_chk_wctomb_overflow_aborts(void) {
    char            a[1];
    volatile size_t n = 0;
    __wctomb_chk(a, L'a', n);
}

[[cccc::test(exit_code = 134)]]
void test_chk_realpath_small_buffer_aborts(void) {
    char a[8];
    __realpath_chk("/", a, sizeof a);
}

[[cccc::test(exit_code = 134)]]
void test_chk_poll_overflow_aborts(void) {
    struct pollfd   p[1] = {{0, 0, 0}};
    volatile size_t n    = 4;
    __poll_chk(p, n, 0, sizeof p);
}

[[cccc::test(exit_code = 134)]]
void test_chk_getdomainname_overflow_aborts(void) {
    char            a[4];
    volatile size_t n = 64;
    __getdomainname_chk(a, n, sizeof a);
}

[[cccc::test(exit_code = 134)]]
void test_chk_readlinkat_overflow_aborts(void) {
    char            a[4];
    volatile size_t n = 64;
    __readlinkat_chk(AT_FDCWD, "/proc/self/exe", a, n, sizeof a);
}

[[cccc::test(exit_code = 134)]]
void test_chk_explicit_bzero_overflow_aborts(void) {
    char            a[4];
    volatile size_t n = 16;
    __explicit_bzero_chk(a, n, sizeof a);
}

[[cccc::test(exit_code = 134)]]
void test_chk_getwd_small_buffer_aborts(void) {
    char a[8];
    __getwd_chk(a, sizeof a);
}

[[cccc::test(exit_code = 134)]]
void test_chk_fgets_unlocked_overflow_aborts(void) {
    char            a[4];
    volatile size_t n = 64;
    __fgets_unlocked_chk(a, sizeof a, (int)n, stdin);
}
#endif
