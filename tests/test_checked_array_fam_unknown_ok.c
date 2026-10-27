// CCCC_FLAGS: --checked-pointers
// bounds(unknown) opts a checked flexible array member out of the check and
// satisfies the checked-region array rule.

#include <stdlib.h>

struct S {
    int n;
    int d _Checked[] [[cccc::bounds(unknown)]];
};

int main(void) {
    struct S *s = malloc(sizeof(struct S) + 8 * sizeof(int));
    s->n        = 1;
    s->d[3]     = 42;
    return s->d[3];
}
