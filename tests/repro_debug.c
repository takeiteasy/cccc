// Debug: print values during nested varargs
#include "stdarg.h"
#include "stdio.h"

int inner_vararg(int n, ...) {
    va_list args;
    va_start(args, n);
    printf("  inner: n=%d, reg_count=%d\n", n, args.reg_count);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        int v = va_arg(args, int);
        printf("  inner: va_arg[%d]=%d, reg_count now=%d\n", i, v,
               args.reg_count);
        sum += v;
    }
    va_end(args);
    printf("  inner: returning sum=%d\n", sum);
    return sum;
}

int outer_vararg(int count, ...) {
    va_list args;
    va_start(args, count);
    printf("outer: count=%d, reg_count=%d\n", count, args.reg_count);
    int total = 0;
    for (int i = 0; i < count; i++) {
        int val = va_arg(args, int);
        printf("outer: va_arg[%d]=%d, reg_count now=%d\n", i, val,
               args.reg_count);
        int r = inner_vararg(2, val, val * 2);
        printf("outer: inner returned %d\n", r);
        total += r;
    }
    va_end(args);
    return total;
}

int main() {
    int result = outer_vararg(3, 1, 2, 3);
    printf("Result: %d (expected 18)\n", result);
    if (result == 18)
        return 42;
    return result;
}
