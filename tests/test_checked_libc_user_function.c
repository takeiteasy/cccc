// CCCC_FLAGS: --checked-pointers
// A program's own function that shares a libc name is not given libc bounds.
#include <stddef.h>

static long read(int fd, char *buf, size_t n) {
    return (long)(fd + n) + buf[0] - buf[0];
}

int main(void) {
    char            a[4] = {0};
    volatile size_t n    = 100;
    return read(1, a, n) == 101 ? 42 : 1;
}
