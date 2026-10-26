// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A non-null store into the terminator slot of a checked flexible array
// member traps.

#include <stdlib.h>

struct S {
    int  n;
    char d _Nt_checked[] [[cccc::count(n)]];
};

int main(void) {
    struct S *s    = malloc(sizeof(struct S) + 4);
    s->n           = 3;
    volatile int i = 3;
    s->d[i]        = 7;
    return 42;
}
