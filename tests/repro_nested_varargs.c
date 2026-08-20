// Minimal nested varargs test
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
        int val = va_arg(args, int);
        // Call another variadic function
        total += inner_vararg(2, val, val * 2);
    }

    va_end(args);
    return total;
}

int main() {
    // outer_vararg(3, 1, 2, 3) = inner(2,1,2) + inner(2,2,4) + inner(2,3,6)
    //                          = 3 + 6 + 9 = 18
    int result = outer_vararg(3, 1, 2, 3);
    if (result == 18)
        return 42;
    return result; // Return actual result for debugging
}
