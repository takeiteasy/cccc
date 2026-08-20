// Simpler nested varargs test - verify iteration count
#include "stdarg.h"

int simple_sum(int n, ...) {
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
    // First test: simple_sum(3, 10, 20, 30) should be 60
    int r1 = simple_sum(3, 10, 20, 30);
    if (r1 != 60)
        return 1;

    // Second test: call simple_sum inside a loop
    int total = 0;
    for (int i = 1; i <= 3; i++) {
        int r  = simple_sum(2, i, i * 2); // 3, 6, 9
        total += r;
    }
    // total should be 3 + 6 + 9 = 18
    if (total == 18)
        return 42;
    return total;
}
