// Test: va_arg result passed to nested varargs
#include "stdarg.h"

int inner(int n, ...) {
    va_list args;
    va_start(args, n);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += va_arg(args, int);
    }
    va_end(args);
    return sum;
}

int outer(int count, ...) {
    va_list args;
    va_start(args, count);

    int val = va_arg(args, int); // Get first arg

    // Use val in inner call
    int r = inner(2, val, val * 2); // If val=1, should be 1+2=3

    va_end(args);
    return r;
}

int main() {
    // outer(1, 1) should return inner(2, 1, 2) = 3
    int result = outer(1, 1);
    if (result == 3)
        return 42;
    return result;
}
