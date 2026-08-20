// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #653: CHKT3's effective-type shadow is now byte-granular, so a
// type confusion at a struct member's offset (not just the allocation's
// base pointer) is caught. Writing s->b as float, then reading the same
// bytes back through a mismatched pointer type, must be flagged.
#include <stdlib.h>

struct S {
    int   a;
    float b;
};

int main(void) {
    struct S *s = malloc(sizeof(struct S));
    s->a        = 20;
    s->b        = 22.0f;        // stamps s->b's byte range (offset 4) as float
    int *p      = (int *)&s->b; // same bytes, reinterpreted as int*
    int  result = *p; // load as int: mismatches the stamped float type
    free(s);
    return result;
}
