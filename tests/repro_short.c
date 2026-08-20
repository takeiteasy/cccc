// Same as failing test but with short function names
#include "stdarg.h"

int i(int n, ...) {
    va_list args;
    va_start(args, n);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += va_arg(args, int);
    }
    va_end(args);
    return sum;
}

int o(int count, ...) {
    va_list args;
    va_start(args, count);
    int total = 0;
    for (int j = 0; j < count; j++) {
        int val  = va_arg(args, int);
        total   += i(2, val, val * 2);
    }
    va_end(args);
    return total;
}

int main() {
    int result = o(3, 1, 2, 3);
    if (result == 18)
        return 42;
    return result;
}
