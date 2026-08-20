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

int main() {
    // Just test inner first
    int r = inner(3, 10, 20, 30);
    if (r != 60)
        return r;
    return 42;
}
