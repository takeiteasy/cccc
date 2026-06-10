// CCCC_FLAGS: --testing
#include <stdlib.h>

// Test passes when the test function returns normally (exit code 0).
[[cccc::test(exit_code = 0)]]
int test_normal_exit(void) {
    return 0;
}

// Test passes when the function calls exit() with the expected code.
[[cccc::test(exit_code = 42)]]
void test_explicit_exit(void) {
    exit(42);
}

// Test passes when a null-pointer dereference produces SIGSEGV (128+11=139).
[[cccc::test(exit_code = 139)]]
int test_segfault(void) {
    volatile int *p = (volatile int *)0;
    return *p;
}

