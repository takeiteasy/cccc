// CCCC_FLAGS: -V --type-checks
// Ticket #651: CHKT3 is scoped to base pointers only (offset 0 into the
// allocation) since subobject/member types aren't tracked. Accessing a
// struct member at a non-zero offset (s->b, an interior access) must not
// be checked against the whole allocation's effective type, or a legal
// same-size member of a different type (e.g. an int then a float member)
// would false-positive.
#include <stdlib.h>

struct S {
    int a;
    float b;
};

int main(void) {
    struct S *s = malloc(sizeof(struct S));
    s->a = 20;   // base member (offset 0): stamps effective type int
    s->b = 22.0f; // interior member (offset != 0): skipped by CHKT3
    int result = s->a + (int)s->b;
    free(s);
    return result;
}
