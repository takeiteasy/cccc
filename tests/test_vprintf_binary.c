// Test: %b/%B and multi-arg forwarding via the v*-family
// (vprintf/vsprintf/vsnprintf/vscanf/vsscanf)
// Returns: 42

#include "stdio.h"
#include <stdarg.h>

int my_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

int my_sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int my_sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

int main() {
    // %b/%B single-arg cases
    my_printf("%b\n", 42u);        // 101010
    my_printf("%#B\n", 10u);       // 0B1010

    char buf[64];
    my_sprintf(buf, "%#b", 255u);
    printf("sprintf: %s\n", buf);  // 0b11111111

    int v;
    my_sscanf("0b101", "%b", &v);
    printf("sscanf: %d\n", v);     // 5

    // Multi-arg forwarding (#407): all args after the first must arrive correctly.
    my_printf("[%d] [%d]\n", 6, -5);            // [6] [-5]
    my_printf("[%d] [%f] [%d]\n", 1, 2.5, 3);  // [1] [2.500000] [3]

    char buf2[64];
    my_sprintf(buf2, "%d+%d=%d", 10, 20, 30);
    printf("multi-sprintf: %s\n", buf2);         // 10+20=30

    int a, b, c;
    my_sscanf("7 8 9", "%d %d %d", &a, &b, &c);
    printf("multi-sscanf: %d %d %d\n", a, b, c); // 7 8 9

    return 42;
}
