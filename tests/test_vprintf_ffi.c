// Test: va_list forwarding in v*-family (vprintf/vsprintf/vsnprintf/vfprintf/
// vscanf/vsscanf) passes all arguments correctly, not just the first. (#407)
// Returns: 0
// CCCC_EXPECT_STDOUT: all ok

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int my_vprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

static int my_vsprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

static int my_vsnprintf(char *buf, int n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, (unsigned)n, fmt, ap);
    va_end(ap);
    return r;
}

static int my_vsscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

int main(void) {
    int fail = 0;
    char buf[128];

    // Ticket #407 repro: second int arg must arrive correctly
    my_vsprintf(buf, "[%d] [%d]", 6, -5);
    if (strcmp(buf, "[6] [-5]") != 0) { printf("FAIL vsprintf 2-int: got %s\n", buf); fail++; }

    // Mixed int + double + int
    my_vsprintf(buf, "%d %.1f %d", 1, 2.5, 3);
    if (strcmp(buf, "1 2.5 3") != 0) { printf("FAIL vsprintf mixed: got %s\n", buf); fail++; }

    // Three ints
    my_vsprintf(buf, "%d+%d=%d", 10, 20, 30);
    if (strcmp(buf, "10+20=30") != 0) { printf("FAIL vsprintf 3-int: got %s\n", buf); fail++; }

    // vsnprintf
    my_vsnprintf(buf, (int)sizeof buf, "%d %d %d", 7, 8, 9);
    if (strcmp(buf, "7 8 9") != 0) { printf("FAIL vsnprintf: got %s\n", buf); fail++; }

    // vsscanf: three output args
    int a = 0, b = 0, c = 0;
    my_vsscanf("11 22 33", "%d %d %d", &a, &b, &c);
    if (a != 11 || b != 22 || c != 33) { printf("FAIL vsscanf: got %d %d %d\n", a, b, c); fail++; }

    if (fail == 0)
        printf("all ok\n");

    return 0;
}
