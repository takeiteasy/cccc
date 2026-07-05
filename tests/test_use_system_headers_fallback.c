// --use-system-headers: non-owned std headers fall back to CCCC polyfills
// when no SDK dir is configured (e.g. no --sysroot / -isystem). The flag
// must be accepted and the polyfill used as the fallback source.
// CCCC_FLAGS: --use-system-headers
#include <stdio.h>   // non-owned: falls back to CCCC polyfill
#include <stdarg.h>  // owned: always from CCCC

static int sum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

int main(void) {
    // va_list works (stdarg.h resolved via CCCC's owned copy)
    // stdio.h resolved via polyfill fallback: FILE type available
    FILE *f = 0;
    (void)f;
    return sum(3, 10, 12, 20);  // 10+12+20 = 42
}
