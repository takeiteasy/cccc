// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A checked flexible array member is checked against its bounds(lo, hi) range.

#include <stdlib.h>

struct S {
    int *end;
    int  d _Checked[] [[cccc::bounds(d, end)]];
};

int main(void) {
    struct S *s    = malloc(sizeof(struct S) + 4 * sizeof(int));
    s->end         = s->d + 4;
    volatile int i = 4;
    s->d[i]        = 1;
    return 42;
}
