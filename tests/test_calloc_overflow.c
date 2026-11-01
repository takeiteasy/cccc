#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    errno = 0;
    if (calloc((size_t)1 << 60, 16) != NULL || errno != ENOMEM)
        return 1;
    errno = 0;
    if (calloc(SIZE_MAX, 2) != NULL || errno != ENOMEM)
        return 2;

    void *(*fn)(size_t, size_t) = calloc;
    errno                       = 0;
    if (fn((size_t)1 << 60, 16) != NULL || errno != ENOMEM)
        return 3;

    int *p = calloc(4, sizeof *p);
    if (!p || p[0] || p[3])
        return 4;
    free(p);
    free(calloc(0, 16));

    char buf[64];
    snprintf(buf, sizeof buf, "%lld %d", LLONG_MIN, INT_MIN);
    if (strcmp(buf, "-9223372036854775808 -2147483648"))
        return 5;
    return 42;
}
