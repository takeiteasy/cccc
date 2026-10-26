// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A checked flexible array member is checked against its sibling count.

#include <stdlib.h>

struct S {
    int n;
    int d _Checked[] [[cccc::count(n)]];
};

int main(void) {
    struct S *s    = malloc(sizeof(struct S) + 4 * sizeof(int));
    s->n           = 4;
    volatile int i = 4;
    s->d[i]        = 1;
    return 42;
}
