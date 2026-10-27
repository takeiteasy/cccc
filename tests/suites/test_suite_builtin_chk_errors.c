// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// __builtin___*_chk is checked like an ordinary call.

#include <string.h>

[[cccc::test(error = "too few arguments")]]
void test_chk_too_few_arguments(void) {
    char a[8];
    __builtin___memcpy_chk(a, a, 8);
}

[[cccc::test(error = "too few arguments")]]
void test_chk_printf_too_few_arguments(void) {
    char a[8];
    __builtin___snprintf_chk(a, 8, 0);
}
