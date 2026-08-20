// CCCC_FLAGS: --testing
// Tests exit_code= assertion in [[cccc::test]]; used by
// test_host_signal_debugger.py to verify that test-mode exit codes don't
// trigger the host-signal debugger.

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
