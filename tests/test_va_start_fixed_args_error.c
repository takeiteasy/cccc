// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
// CCCC_EXPECT_STDERR: 'va_start' used in function with fixed arguments

#include <stdarg.h>

int f(int n) {
    va_list ap;
    va_start(ap);
    va_end(ap);
    return n;
}

int main(void) {
    return f(1);
}
