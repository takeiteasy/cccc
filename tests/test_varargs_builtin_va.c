/*
 * Test __builtin_va_* macros (ticket #513)
 *
 * These are aliases defined in <stdarg.h> that forward to the va_* macros.
 */

#include <stdarg.h>

static int sum(int n, ...) {
    va_list ap;
    __builtin_va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return total;
}

static int copy_sum(int n, ...) {
    va_list ap, ap2;
    __builtin_va_start(ap, n);
    __builtin_va_copy(ap2, ap);
    // consume from copy
    int total = 0;
    for (int i = 0; i < n; i++)
        total += __builtin_va_arg(ap2, int);
    __builtin_va_end(ap2);
    __builtin_va_end(ap);
    return total;
}

int main() {
    if (sum(3, 1, 2, 3) != 6) return 1;
    if (sum(0) != 0) return 2;
    if (copy_sum(4, 10, 20, 30, 40) != 100) return 3;
    return 42;
}
