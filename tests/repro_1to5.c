// Extract tests 1-4 from test_varargs_int.c then test 5
#include "stdarg.h"

// Test 1: Simple variadic function - sum integers
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

// Test 2: va_copy - copy va_list
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
        int val  = va_arg(args, int);
        total   += inner_vararg(2, val, val * 2);
    }
    va_end(args);
    return total;
}

int main() {
    int result;

    // Test 1: Simple sum
    result = sum_ints(3, 10, 20, 30);
    if (result != 60)
        return 1;

    // Test 2: Different counts
    result = sum_ints(5, 1, 2, 3, 4, 5);
    if (result != 15)
        return 2;

    // Test 3: Single argument
    result = sum_ints(1, 100);
    if (result != 100)
        return 3;

    // Test 4: va_copy
    result = test_va_copy(4, 10, 20, 30, 40);
    if (result != 100)
        return 4;

    // Test 5: Nested variadic calls - this is failing
    result = outer_vararg(3, 1, 2, 3);
    if (result != 18)
        return 5;

    return 42;
}
