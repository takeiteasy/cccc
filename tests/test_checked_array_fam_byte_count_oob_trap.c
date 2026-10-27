// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A checked flexible array member is checked against its sibling byte count.

#include <stdlib.h>

struct S {
    int bytes;
    int d _Checked[] [[cccc::byte_count(bytes)]];
};

int main(void) {
    struct S *s    = malloc(sizeof(struct S) + 4 * sizeof(int));
    s->bytes       = 4 * sizeof(int);
    volatile int i = 4;
    s->d[i]        = 1;
    return 42;
}
