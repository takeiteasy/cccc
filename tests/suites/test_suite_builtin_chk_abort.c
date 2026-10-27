// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: the host's fortify failure signal differs (SIGTRAP on Darwin)
// A __builtin___*_chk call that exceeds a known object size aborts like libc's
// __chk_fail. The volatile count keeps a host compiler from folding the call.

#include <string.h>

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
