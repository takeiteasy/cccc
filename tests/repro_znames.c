// Test: same as repro_nested5 but with long names (exact copy of passing)
#include "stdarg.h"

int zinner(int n, ...) {
    va_list args;
    va_start(args, n);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += va_arg(args, int);
    }
    va_end(args);
    return sum;
}

int zouter(int count, ...) {
    va_list args;
    va_start(args, count);

    int total = 0;
    for (int i = 0; i < count; i++) {
        int val  = va_arg(args, int);
        int r    = zinner(2, val, val * 2);
        total   += r;
    }

    va_end(args);
    return total;
}

int main() {
    int result = zouter(3, 1, 2, 3);
    if (result == 18)
        return 42;
    return result;
}
