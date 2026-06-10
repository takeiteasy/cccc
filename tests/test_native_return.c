// Native mode return value assertions.
// CCCC_FLAGS: --testing

[[cccc::test(mode = "native", return = 42)]]
int test_native_ret_int(void) {
    return 42;
}

[[cccc::test(mode = "native", return = 0)]]
int test_native_ret_zero(void) {
    return 0;
}

[[cccc::test(mode = "native", return = 3.14, return_epsilon = 0.01)]]
double test_native_ret_float(void) {
    return 3.14159;
}

[[cccc::test(mode = "native", return = "hello")]]
const char *test_native_ret_str(void) {
    return "hello";
}

[[cccc::test(mode = "native", return != 0)]]
int test_native_ret_neq(void) {
    return 99;
}

[[cccc::test(mode = "native", return > 10)]]
int test_native_ret_gt(void) {
    return 50;
}
