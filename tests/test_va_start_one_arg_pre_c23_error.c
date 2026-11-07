// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: expected ','
//
// va_start(ap) without `last` is C23-only; earlier standards keep the
// two-argument form.

#include <stdarg.h>

int f(int n, ...) {
    va_list ap;
    va_start(ap);
    va_end(ap);
    return n;
}

int main(void) {
    return f(1);
}
