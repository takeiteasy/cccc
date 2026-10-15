// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// A call that would raise an exception or set errno is not folded, so it is
// not a constant initializer.

[[cccc::test(error = "not a compile-time constant")]]
int test_sqrt_negative(void) {
    static double g = __builtin_sqrt(-1.0);
    return (int)g;
}

[[cccc::test(error = "not a compile-time constant")]]
int test_sqrt_infinity(void) {
    static double g = __builtin_sqrt(__builtin_inf());
    return (int)g;
}

[[cccc::test(error = "not a compile-time constant")]]
int test_fma_overflow(void) {
    static double g = __builtin_fma(1e308, 10.0, 0.0);
    return (int)g;
}

[[cccc::test(error = "not a compile-time constant")]]
int test_fma_invalid(void) {
    static double g = __builtin_fma(__builtin_inf(), 0.0, 1.0);
    return (int)g;
}
