#include <stdlib.h>
#include <string.h>

// A guest call in a later argument of a VM-heap builtin must not clobber the
// earlier arguments.

static unsigned long four(void) {
    return 4;
}

int main(void) {
    unsigned long c = 3;
    char         *p = realloc(NULL, c * four());
    if (!p)
        return 1;
    memset(p, 7, c * four());
    p = realloc(p, (c + 1) * four());
    if (!p || p[0] != 7)
        return 2;
    int *q = calloc(c, four());
    if (!q || q[0] != 0)
        return 3;
    q = reallocarray(q, c, four() * 2);
    if (!q)
        return 4;
    void *a = aligned_alloc(16, four() * 16);
    if (!a)
        return 5;
    free(a);
    free(q);
    free(p);
    return 42;
}
