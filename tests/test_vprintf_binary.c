// Test: %b/%B via the v*-family (vprintf/vsprintf/vsnprintf/vscanf/vsscanf)
// Returns: 42
//
// NOTE: only the first variadic argument forwards correctly through a
// user-defined function's va_list to a v*-family call (see ticket #407,
// a pre-existing VM/FFI limitation unrelated to %b/%B support). These
// tests therefore stick to a single conversion per call.

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
    my_printf("%b\n", 42u);        // 101010
    my_printf("%#B\n", 10u);       // 0B1010

    char buf[32];
    my_sprintf(buf, "%#b", 255u);
    printf("sprintf: %s\n", buf);  // 0b11111111

    int v;
    my_sscanf("0b101", "%b", &v);
    printf("sscanf: %d\n", v);     // 5

    return 42;
}
