// Exact copy of failing test 5 from test_varargs_int.c
#include "stdarg.h"

// Test 3: Nested variadic calls
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
    int result;

    // Test 5: Nested variadic calls
    // outer_vararg(3, 1, 2, 3) = inner_vararg(2,1,2) + inner_vararg(2,2,4) +
    // inner_vararg(2,3,6)
    //                          = 3 + 6 + 9 = 18
    result = outer_vararg(3, 1, 2, 3);
    if (result != 18)
        return 5;

    return 42; // All tests passed!
}
