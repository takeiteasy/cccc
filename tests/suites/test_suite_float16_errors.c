// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Uses of a _Float16 that C does not allow.

#include <stdarg.h>

_Float16 global_h;

int sum(int n, ...);

[[cccc::test(error = "invalid operands to %")]]
int test_remainder(void) {
    return (int)(global_h % global_h);
}

[[cccc::test(error = "invalid operands to &")]]
int test_bitwise_and(void) {
    return (int)(global_h & 1);
}

[[cccc::test(error = "invalid operands to <<")]]
int test_shift(void) {
    return (int)(global_h << 1);
}

[[cccc::test(error = "invalid operand to unary ~")]]
int test_complement(void) {
    return (int)~global_h;
}

[[cccc::test(error = "invalid operands to binary expression")]]
int test_index(void) {
    int a[2] = {0, 0};
    return a[global_h];
}

[[cccc::test(error = "cannot be passed through '...'")]]
int test_variadic_argument(void) {
    return sum(1, global_h);
}

[[cccc::test(error = "cannot be passed through '...'")]]
int test_variadic_host_call(void) {
    extern int printf(const char *, ...);
    return printf("%f", global_h);
}
