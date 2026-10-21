// A va_list passed as a parameter is by-value under the VM and -c=native.
#include <stdarg.h>
static int consume_one(int n, va_list ap) {
    (void)n;
    return va_arg(ap, int);
}
static int forward(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int first    = va_arg(ap, int);
    int consumed = consume_one(1, ap);
    int next     = va_arg(ap, int);
    va_end(ap);
    if (first != 10)
        return 1;
    if (consumed != 20)
        return 2;
    if (next != 20)
        return 3;
    return 0;
}
int main(void) {
    return forward(2, 10, 20) == 0 ? 42 : 1;
}
