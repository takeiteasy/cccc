#include "stdio.h"

// Simple test to understand va_start address calculation
int test_varargs(int a, int b, ...) {
    printf("a = %d, b = %d\n", a, b);
    printf("&a = %p, &b = %p\n", &a, &b);
    printf("&b - 1 = %p\n", (long long *)&b - 1);

    // Read what's at &b - 1
    long long *ptr = (long long *)&b - 1;
    printf("Value at &b - 1 = %lld\n", *ptr);
    printf("Value at &b - 2 = %lld\n", *(ptr - 1));
    printf("Value at &b - 3 = %lld\n", *(ptr - 2));

    return 42;
}

int main() {
    return test_varargs(10, 20, 30, 40, 50);
}
