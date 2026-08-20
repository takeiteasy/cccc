// Remove sum_ints and test_va_copy entirely
#include "stdarg.h"

int inner_vararg(int n, ...) {
    va_list args;
    va_start(args, n);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += va_arg(args, int);
    }
    va_end(args);
    return sum;
}

int outer_vararg(int count, ...) {
    va_list args;
    va_start(args, count);
    int total = 0;
    for (int i = 0; i < count; i++) {
        int val  = va_arg(args, int);
        total   += inner_vararg(2, val, val * 2);
    }
    va_end(args);
    return total;
}

int main() {
    int result = outer_vararg(3, 1, 2, 3);
    if (result == 18)
        return 42;
    return result;
}
