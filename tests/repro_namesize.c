// Minimal test - compare stack sizes for short vs long function names
#include "stdarg.h"

// Short name version
int a(int n, ...) {
    va_list args;
    va_start(args, n);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += va_arg(args, int);
    }
    va_end(args);
    return sum;
}

// Long name version
int abcdefghij(int n, ...) {
    va_list args;
    va_start(args, n);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += va_arg(args, int);
    }
    va_end(args);
    return sum;
}

int main() {
    int r1 = a(3, 10, 20, 30);
    int r2 = abcdefghij(3, 10, 20, 30);
    if (r1 == 60 && r2 == 60)
        return 42;
    if (r1 != 60)
        return 1;
    return 2;
}
