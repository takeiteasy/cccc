// CCCC_FLAGS: --checked-pointers
// A user definition of a libc-named function is not bounded by the built-in
// libc table, which describes callers.

#include <stddef.h>
#include <string.h>

void *memchr(const void *s, int c, size_t len) {
    const unsigned char *p = s;
    for (size_t i = 0; i < len; i++)
        if (p[i] == (unsigned char)c)
            return (void *)(p + i);
    return NULL;
}

int main(void) {
    char buf[4] = {1, 2, 3, 4};
    return memchr(buf, 3, 4) == buf + 2 ? 42 : 1;
}
