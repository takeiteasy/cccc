// Test: va_copy functionality
#include "stdarg.h"

int test_va_copy(int count, ...) {
    va_list args1, args2;
    va_start(args1, count);
    va_copy(args2, args1);

    // Process with first list
    int sum1 = 0;
    for (int i = 0; i < count; i++) {
        sum1 += va_arg(args1, int);
    }

    // Process with copied list (should get same values)
    int sum2 = 0;
    for (int i = 0; i < count; i++) {
        sum2 += va_arg(args2, int);
    }

    va_end(args1);
    va_end(args2);

    // Both sums should be equal
    if (sum1 != sum2)
        return -1;
    return sum1;
}

int main() {
    // test_va_copy(4, 10, 20, 30, 40) should return 100
    int result = test_va_copy(4, 10, 20, 30, 40);
    if (result == 100)
        return 42;
    return result;
}
