// A va_list handed to a libc function round-trips under -c=native.
#include <stdio.h>
#include <stdarg.h>
static int test_vsnprintf_basic(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int  first = va_arg(ap, int);
    char buf[32];
    vsnprintf(buf, sizeof buf, "%d", ap);
    int second = va_arg(ap, int);
    va_end(ap);
    if (first != 10)
        return 1;
    if (buf[0] != '2' || buf[1] != '0' || buf[2] != 0)
        return 2;
    if (second != 20)
        return 3;
    return 0;
}
int main(void) {
    return test_vsnprintf_basic(2, 10, 20) == 0 ? 42 : 1;
}
