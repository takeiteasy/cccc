// Test nested varargs - varargs called from within varargs function
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

    int total = 0;
    int val   = va_arg(args, int); // Get first arg

    // Call inner with hardcoded args
    int r  = inner(2, 10, 20); // Should be 30
    total += r;
    total += val;              // Add first arg from outer

    va_end(args);
    return total;
}

int main() {
    // outer(1, 5) should return inner(2, 10, 20) + 5 = 30 + 5 = 35
    int result = outer(1, 5);
    if (result == 35)
        return 42;
    return result;
}
