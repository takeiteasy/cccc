// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Any use of a _Float16 value is rejected; only storage is supported.

extern _Float16 give(void);
extern int take(_Float16 x);
_Float16 global_h;

[[cccc::test(error = "_Float16 values are not supported")]]
int test_arithmetic(void) {
    _Float16 *p = &global_h;
    return (int)(*p + *p);
}

[[cccc::test(error = "_Float16 values are not supported")]]
int test_increment(void) {
    global_h++;
    return 0;
}

[[cccc::test(error = "_Float16 values are not supported")]]
int test_conversion(void) {
    float f = global_h;
    return (int)f;
}

[[cccc::test(error = "_Float16 values are not supported")]]
int test_assignment(void) {
    global_h = 1;
    return 0;
}

[[cccc::test(error = "_Float16 values are not supported")]]
int test_local_initializer(void) {
    _Float16 h = 1;
    return 0;
}

[[cccc::test(error = "_Float16 values are not supported")]]
int test_condition(void) {
    if (global_h)
        return 1;
    return 0;
}

[[cccc::test(error = "_Float16 values are not supported")]]
int test_call_argument(void) {
    return take(global_h);
}

[[cccc::test(error = "_Float16 values are not supported")]]
int test_call_result(void) {
    give();
    return 0;
}

[[cccc::test(error = "_Float16 values are not supported")]]
int test_cast_to_void(void) {
    (void)global_h;
    return 0;
}
