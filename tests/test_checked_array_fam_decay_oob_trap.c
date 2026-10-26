// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A pointer taken from a checked flexible array member keeps its count.

#include <stdlib.h>

struct S {
    int n;
    int d _Checked[] [[cccc::count(n)]];
};

int main(void) {
    struct S *s    = malloc(sizeof(struct S) + 2 * sizeof(int));
    s->n           = 2;
    int *p         = s->d;
    volatile int i = 2;
    return p[i];
}
