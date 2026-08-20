// CCCC_FLAGS: --type-checks
// Ticket #653: realloc always produces a fresh bump allocation in CCCC's
// VM heap (never grows in place), so op_REALC_fn must carry the old
// block's effective-type shadow across to the new address the same way
// MCPY does for a compiler-generated copy. Growing a struct's allocation
// and then reading its pre-existing members back must not false-positive.
#include <stdlib.h>

struct S {
    int   a;
    float b;
};

int main(void) {
    struct S *s = malloc(sizeof(struct S));
    s->a        = 10;
    s->b        = 5.0f;

    s = realloc(s, sizeof(struct S) * 4); // grows -- always a fresh block
    int result  = s->a + (int)s->b; // must still match the propagated types

    s[1].a      = 3;                // touch the newly-grown tail too
    result     += s[1].a;

    free(s);
    return result == 18 ? 42 : 1;
}
