// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: 'va_start' used in function with fixed arguments
// CCCC_EXPECT_STDERR: test_va_start_unprototyped_error\.c:14:
// CCCC_REJECT_STDERR: stdarg\.h:[0-9]+:[^0-9]
//
// A pre-C23 `int f()` definition is flagged variadic internally, but it has
// no `...`, so va_start in it is rejected like in any fixed-argument function.

#include <stdarg.h>

int f() {
    va_list ap;
    va_start(ap, ap);
    va_end(ap);
    return 1;
}

int main(void) {
    return f();
}
