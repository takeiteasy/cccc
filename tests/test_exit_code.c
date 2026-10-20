// CCCC_LEAKS_SKIP: forks that hang under leaks -atExit
// CCCC_FLAGS: --testing
// Tests exit_code= assertion in [[cccc::test]].

#include <stdlib.h>

[[cccc::test(exit_code = 0)]]
int test_normal_exit(void) {
    return 0;
}

[[cccc::test(exit_code = 42)]]
void test_explicit_exit(void) {
    exit(42);
}

[[cccc::test(exit_code = 139)]]
int test_segfault(void) {
    volatile int *p = (volatile int *)0;
    return *p;
}
