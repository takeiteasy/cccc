// CCCC_FLAGS: --use-system-headers
// va_list is one type whichever of <stdio.h>/<stdarg.h> comes first, and
// forwards to the v*printf family.

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int first_int(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int v = va_arg(ap, int);
    va_end(ap);
    return v;
}

static int format(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int main(void) {
    char buf[32];
    if (format(buf, sizeof buf, "%d-%s", 7, "x") != 3 || strcmp(buf, "7-x"))
        return 1;
    return first_int("x", 42);
}
