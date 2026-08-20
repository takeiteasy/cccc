// Just include the functions but only run test 5
#include "stdarg.h"

int sum_ints(int count, ...) {
    va_list args;
    va_start(args, count);
    int sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);
    }
    va_end(args);
    return sum;
}

int test_va_copy(int count, ...) {
    va_list args1, args2;
    va_start(args1, count);
    va_copy(args2, args1);
    int sum1 = 0;
    for (int i = 0; i < count; i++) {
        sum1 += va_arg(args1, int);
    }
    int sum2 = 0;
    for (int i = 0; i < count; i++) {
        sum2 += va_arg(args2, int);
    }
    va_end(args1);
    va_end(args2);
    return (sum1 == sum2) ? sum1 : -1;
}

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
    // ONLY run test 5 - skip tests 1-4
    int result = outer_vararg(3, 1, 2, 3);
    if (result == 18)
        return 42;
    return result;
}
