// <stdarg.h> guards round-trip under -c=native.
#include <stdio.h>
#include <stdarg.h>

static int sum_ints(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

int main(void) {
    FILE *f = 0;
    (void)f;
    if (sum_ints(3, 10, 12, 20) != 42)
        return 1;
    return 42;
}
